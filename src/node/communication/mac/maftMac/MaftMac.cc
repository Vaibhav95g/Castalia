#include "MaftMac.h"

Define_Module(MaftMac);

void MaftMac::startup(){

	// variable init
	boundTo = -1;
	dataChannel = -1;
	dataPair = -1;
	pktsTxed = 0;
	pktsRxed = 0;
	del_t = 0;
	newCH = -1;


	//--- init Mobility Manager ---//
	initMobilityManager();

	trace() << "NODE:" << SELF_MAC_ADDRESS << " startup()";
	nodeType = par("nodeType");
	trace() << "NODE:" << SELF_MAC_ADDRESS << " is type:" << nodeType;

	// set channel to MFT_CNTL_CHANNEL
	toRadioLayer(createRadioCommand(SET_CARRIER_FREQ, ((MFT_CNTL_CHANNEL-10)*5) + 2400) );
	
	if(nodeType == CLUSTER_HEAD)
		setTimer(WAKE_TO_SYNC, 1);
	else{

		if(SELF_MAC_ADDRESS % 2 == 0)
			macType = REC;
		else
			macType = SEN;

		toRadioLayer(createRadioCommand(SET_STATE,RX));
		trace() << "NODE:" << SELF_MAC_ADDRESS << " in RX";
	}

}



void MaftMac::fromNetworkLayer(cPacket * pkt, int destination) { //This will only be used in Source, no one else generates packets

}


void MaftMac::fromRadioLayer(cPacket * pkt, double rssi, double lqi){


	MaftPacket *incPacket = dynamic_cast <MaftPacket*>(pkt);
	
	if(incPacket->getPtype() == SYNC_PKT && nodeType == MOBILE_NODE && rssi > -89){

		// reinitialize variables
		pktsTxed = 0;
		pktsRxed = 0;
		dataChannel = -1;
		dataPair = -1;

		boundTo = incPacket->getSource();	

		trace() << "NODE_" << SELF_MAC_ADDRESS << " : Received SYNC from " << boundTo;
		trace() << "Time value : " << incPacket->getTime_val();
		setTimer(WAKE_TO_REC_SCHED,(MFT_SLOT * META_P) + incPacket->getTime_val() - (2*MFT_MINI_SLOT) + del_t);
		//setTimer(WAKE_TO_SEND_META,( (int)uniform(1,40) * MFT_MINI_SLOT ) + incPacket->getTime_val()  );
		setTimer(WAKE_TO_SEND_META,( SELF_MAC_ADDRESS * 5 * MFT_MINI_SLOT ) + incPacket->getTime_val()  );
		toRadioLayer(createRadioCommand(SET_STATE,SLEEP));
		return;
	}

	if(incPacket->getPtype() == META_PKT && nodeType == CLUSTER_HEAD && rssi > -91 && phase == P_META){
		trace() << "NODE_" << SELF_MAC_ADDRESS << " : Received META from " << incPacket->getSource() << " @ (" << incPacket->getX() << "," << incPacket->getY() << ")" << " moving at " << incPacket->getV() << " in " << incPacket->getAngle() << " dir";

		if(incPacket->getSource() % 2 == 0)
			rxBuffer.push_back(MNode(incPacket->getSource(),incPacket->getX(),incPacket->getY()));	
		else
			txBuffer.push_back(MNode(incPacket->getSource(),incPacket->getX(),incPacket->getY()));	

		return;
	}

	if(incPacket->getPtype() == SCHED_PKT && nodeType == MOBILE_NODE && boundTo == incPacket->getSource()){
		trace() << "NODE_" << SELF_MAC_ADDRESS << " : Received SCHED_PKT from " << incPacket->getSource();
		boundTo = -1;

		// -- time correction -- //
		del_t = 0.8 * (node_x_sched_wakeup - incPacket->getDel_t());
		// -------------------- //

		// check if new cluster head is elected
		newCH = incPacket->getNewCH();
		if(newCH != -1){
			if( newCH == SELF_MAC_ADDRESS ){
				nodeType = CLUSTER_HEAD;
				trace() << "I, NODE_" << SELF_MAC_ADDRESS << " am the new CH";
				trace() << "All hail NODE_" << newCH;
			}
			else{
				trace() << "NODE_" << SELF_MAC_ADDRESS << " accepts " << newCH << " as the new CH"; 
				boundTo = CLUSTER_HEAD;
			}
		}

		if(SELF_MAC_ADDRESS % 2 == 1){

			trace() << "I, NODE_" << SELF_MAC_ADDRESS << " am a SENDER";

			for(int i=0;i< incPacket->getPair_count();i++)
				if( incPacket->getTxer(i) == SELF_MAC_ADDRESS ){
					dataPair = incPacket->getRxer(i);
					dataChannel = incPacket->getChannel(i);
					trace() << "NODE_" << SELF_MAC_ADDRESS << " DataPair:" << dataPair << " DataChannel:" << dataChannel;
					trace() << "setting channel to " << dataChannel << " freq: " << ((dataChannel-10)*5) + 2400 ;
					toRadioLayer(createRadioCommand(SET_CARRIER_FREQ, ((dataChannel-10)*5) + 2400) );
					pktsTxed = 0;
					setTimer(WAKE_TO_SEND_DATA,MFT_MINI_SLOT*3);
					return;
				}
		}
		else {
			trace() << "I, NODE_" << SELF_MAC_ADDRESS << " am a Receiver";
			for(int i=0;i< incPacket->getPair_count();i++)
				if( incPacket->getRxer(i) == SELF_MAC_ADDRESS ){
					dataPair = incPacket->getTxer(i);
					dataChannel = incPacket->getChannel(i);
					trace() << "NODE_" << SELF_MAC_ADDRESS << " DataPair:" << dataPair << " DataChannel:" << dataChannel;
					trace() << "setting channel to " << dataChannel << " freq: " << ((dataChannel-10)*5) + 2400 ;
					toRadioLayer(createRadioCommand(SET_CARRIER_FREQ, ((dataChannel-10)*5) + 2400) );
					setTimer(WAKE_TO_RX,MFT_MINI_SLOT*3);
					setTimer(DATA_TRANSFER_TIMEOUT,2*MFT_SLOT);
					return;
				}
		}

		return;
	}

	if(SELF_MAC_ADDRESS % 2 == 0 && incPacket->getPtype() == DATA_PKT && incPacket->getDestination() == SELF_MAC_ADDRESS && incPacket->getSource() == dataPair){
			trace() << "NODE_" << SELF_MAC_ADDRESS << " received DATA_PKT from " << incPacket->getSource();
			pktsRxed++;
			if(pktsRxed > 9){
				pktsRxed = 0;
				// if 10 data packets are received, switch to control channel
				cancelTimer(DATA_TRANSFER_TIMEOUT); // --- Cancel timeout mechanism --- //
				toRadioLayer(createRadioCommand(SET_CARRIER_FREQ, ((MFT_CNTL_CHANNEL-10)*5) + 2400) );
				if(nodeType == CLUSTER_HEAD)// ------- if i'm a cluster head (elected by the ex-CH) -------
					setTimer(WAKE_TO_SYNC,MFT_MINI_SLOT);// -- I should start synchronization before ex-CH --
			}
			
			return;
	}

	return;
}



void MaftMac::sendPacket(MaftPacket *packet) {

	packet->setSource(SELF_MAC_ADDRESS);
	toRadioLayer(packet);
	toRadioLayer(createRadioCommand(SET_STATE, TX));
}


void MaftMac::sendMeta(){

	MaftPacket *metaPkt;
	metaPkt = new MaftPacket("Meta Packet", MAC_LAYER_PACKET);
	metaPkt->setDestination(boundTo);
	metaPkt->setPtype(META_PKT);
	getSelfLocation(x,y);
	metaPkt->setX(x);
	metaPkt->setY(y);
	metaPkt->setV(getSpeed());
	metaPkt->setAngle(getDirection());
	sendPacket(metaPkt);
}



void MaftMac::broadcastSync(double time_val){

	MaftPacket *syncPacket;
	syncPacket = new MaftPacket("Sync Packet", MAC_LAYER_PACKET);
	syncPacket->setDestination(BROADCAST_MAC_ADDRESS);
	syncPacket->setPtype(SYNC_PKT);
	syncPacket->setTime_val(time_val);
	sendPacket(syncPacket);
}

void MaftMac::broadcastSched(double time_val){

	MaftPacket *schedPkt;
	schedPkt = new MaftPacket("Sync Packet", MAC_LAYER_PACKET);
	schedPkt->setDestination(BROADCAST_MAC_ADDRESS);
	schedPkt->setPtype(SCHED_PKT);
	schedPkt->setTime_val(time_val);
	schedPkt->setDel_t(node_0_sched_wakeup);

	// if new cluster head is chosen
	schedPkt->setNewCH(newCH);

	schedPkt->setPair_count(txBuffer.size());
	// construct schedule
	for(int i=0;i<txBuffer.size();i++){
		schedPkt->setTxer(i,txBuffer[i].address);
		schedPkt->setRxer(i,rxBuffer[i].address);
		schedPkt->setChannel(i,12+i);
		trace() << " [" << i << "] " << txBuffer[i].address << " -> " << rxBuffer[i].address << " @" << 12+i;
	}
	sendPacket(schedPkt);
}

void MaftMac::sendData(){

	MaftPacket *dataPkt;
	dataPkt = new MaftPacket("Meta Packet", MAC_LAYER_PACKET);
	dataPkt->setDestination(dataPair);
	dataPkt->setPtype(DATA_PKT);
	sendPacket(dataPkt);
}


void MaftMac::timerFiredCallback(int timer) {

	switch(timer){

		// ---------- CLUSTER HEAD -------------//
		case WAKE_TO_SYNC:
			trace() << "NODE_" << SELF_MAC_ADDRESS << " WAKE_TO_SYNC";
			phase = P_SYNC;
			setTimer(WAKE_TO_META,MFT_SLOT);
			broadcastSync( getTimer(WAKE_TO_META).dbl() - simTime().dbl() );
			break;


		case WAKE_TO_META:
			trace() << "NODE_" << SELF_MAC_ADDRESS << " WAKE_TO_META";
			phase = P_META;
			setTimer(WAKE_TO_SCHED,META_P * MFT_SLOT);
			toRadioLayer(createRadioCommand(SET_STATE,RX));
			break;

		case WAKE_TO_SCHED:
			node_0_sched_wakeup = simTime().dbl();
			trace() << "NODE_" << SELF_MAC_ADDRESS << " WAKE_TO_SCHED";
			phase = P_SCHED;
			newCH = chooseNewCH();
			broadcastSched( getTimer(WAKE_TO_SYNC).dbl() - simTime().dbl() );
			//broadcastSched( getTimer(WAKE_TO_SYNC).dbl() - simTime().dbl() );
			if(newCH != -1){
				trace() << "New CH : " << newCH << " chosen; changing to MOBILE_NODE";
				nodeType = MOBILE_NODE;
				setTimer(WAKE_TO_RX,2*MFT_MINI_SLOT);
			}
			else
				setTimer(WAKE_TO_SYNC,SCHED_P * MFT_SLOT);
			rxBuffer.erase(rxBuffer.begin(),rxBuffer.end());
			txBuffer.erase(txBuffer.begin(),txBuffer.end());
			break;



	 // ----------- MOBILE NODE -------------- //
		case WAKE_TO_SEND_META:
			trace() << "NODE_" << SELF_MAC_ADDRESS << " WAKE_TO_SEND_META";
			sendMeta();
			setTimer(WAKE_TO_SLEEP,MFT_MINI_SLOT);
			//setTimer(WAKE_TO_RX,MFT_MINI_SLOT);
			break;

		case WAKE_TO_REC_SCHED:
			node_x_sched_wakeup = simTime().dbl();
			trace() << "NODE_" << SELF_MAC_ADDRESS << " WAKE_TO_REC_SCHED";
			toRadioLayer(createRadioCommand(SET_STATE,RX));
			break;

		case WAKE_TO_SEND_DATA:
			//trace() << "NODE_" << SELF_MAC_ADDRESS << " WAKE_TO_SEND_DATA";
			if(pktsTxed < MFT_NUM_DATA_PKTS){
				setTimer(WAKE_TO_SEND_DATA,MFT_MINI_SLOT);
				sendData();
				pktsTxed++;
			}
			else{//-- after sending out 10 packets --//
				//----- switch to control channel ----//
				toRadioLayer(createRadioCommand(SET_CARRIER_FREQ, ((MFT_CNTL_CHANNEL-10)*5) + 2400) );
				setTimer(WAKE_TO_RX,MFT_MINI_SLOT);
			}
			break;

			//-- when a receiver doesn't receive 10 packets --//
			//----- and the time alloted for data transfer --//
			//--------- has come to an end------------------//
		case DATA_TRANSFER_TIMEOUT:
			toRadioLayer(createRadioCommand(SET_CARRIER_FREQ, ((MFT_CNTL_CHANNEL-10)*5) + 2400) );
			setTimer(WAKE_TO_RX,MFT_MINI_SLOT);
			break;


	 // -------------- Common ---------------//
		case WAKE_TO_RX:
			trace() << "NODE_" << SELF_MAC_ADDRESS << " in RX";
			toRadioLayer(createRadioCommand(SET_STATE,RX));
			break;

		case WAKE_TO_SLEEP:
			trace() << "NODE_" << SELF_MAC_ADDRESS << " in SLEEP";
			toRadioLayer(createRadioCommand(SET_STATE,SLEEP));
			break;
	}


}

MNode::MNode(int a, int x_val, int y_val){
	address = a;
	x = x_val;
	y = x_val;
	active = false;
}

void MaftMac::initMobilityManager(){
	//nodeMobilityModule = check_and_cast<VirtualMobilityManager*>(getParentModule()->getParentModule()->getParentModule()->getSubmodule("node",SELF_MAC_ADDRESS)->getSubmodule("MobilityManager"));
	nodeMobilityModule = check_and_cast\
						<VirtualMobilityManager *>\
						(getParentModule()->getParentModule()->\
							getSubmodule("MobilityManager"));
}

void MaftMac::getSelfLocation(int& x, int& y) {
	x = nodeMobilityModule->getLocation().x;
	y = nodeMobilityModule->getLocation().y;
}

double MaftMac::getSpeed(){ return nodeMobilityModule->getSpeed(); }

double MaftMac::getDirection(){ return nodeMobilityModule->getDirection(); }

int MaftMac::channelToFrequency(int channel){ return ((channel-10)*5) + 2400; } 

double MaftMac::distance(double x1, double y1, double x2, double y2){ return sqrt( ((x1-x2)*(x1-x2)) + ((y1-y2)*(y1-y2)) ); }

int MaftMac::chooseNewCH(){

	vector<MNode> boundNodes; 
	boundNodes.reserve( txBuffer.size() + rxBuffer.size() ); // preallocate memory
	boundNodes.insert( boundNodes.end(), txBuffer.begin(), txBuffer.end() );
	boundNodes.insert( boundNodes.end(), rxBuffer.begin(), rxBuffer.end() );

	double centroid_x=0, centroid_y=0;
	for(int i=0;i<boundNodes.size();i++){
		centroid_x += boundNodes[i].x; centroid_y += boundNodes[i].y;
	}

	centroid_x = centroid_x / ( txBuffer.size() + rxBuffer.size() );
	centroid_y = centroid_y / ( txBuffer.size() + rxBuffer.size() );

	double dist = 0;
	double minDist = 10000;
	int bestNode = -1;
	for(int i=0;i<boundNodes.size();i++){
		dist = distance(boundNodes[i].x,boundNodes[i].y,centroid_x,centroid_y);
		if(dist  < minDist){
			bestNode = boundNodes[i].address;
			minDist = dist;
		}
	}
	getSelfLocation(x,y);

	if( minDist < distance(x,y,centroid_x,centroid_y) )
		return bestNode;
	else
		return -1;
}




