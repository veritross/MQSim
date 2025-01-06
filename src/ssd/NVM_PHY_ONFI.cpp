#include "NVM_PHY_ONFI.h"

namespace SSD_Components {
	void NVM_PHY_ONFI::ConnectToTransactionServicedSignal(TransactionServicedHandlerType function)
	{
		connectedTransactionServicedHandlers.push_back(function);
	}

	/*
	* Different FTL components maybe waiting for a transaction to be finished:
	* HostInterface: For user reads and writes
	* Address_Mapping_Unit: For mapping reads and writes
	* TSU: For the reads that must be finished for partial writes (first read non updated parts of page data and then merge and write them into the new page)
	* GarbageCollector: For gc reads, writes, and erases
	*/
	void NVM_PHY_ONFI::broadcastTransactionServicedSignal(NVM_Transaction_Flash* transaction)
	{
		flash_channel_ID_type channelID = transaction->Address.ChannelID;
		flash_chip_ID_type chipID = transaction->Address.ChipID;
		if(transaction->Type == Transaction_Type::READ){
			if(((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite != NULL){
				channelID = ((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->Address.ChannelID;
				chipID = ((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->Address.ChipID;
			}
		}
		for (std::vector<TransactionServicedHandlerType>::iterator it = connectedTransactionServicedHandlers.begin();
			it != connectedTransactionServicedHandlers.end(); it++) {
			(*it)(transaction);
		}

		auto tmp = (NVM_PHY_ONFI*)this;
		if(channelID != transaction->Address.ChannelID || chipID != transaction->Address.ChipID){
			if(tmp->Get_channel_status(channelID) == BusChannelStatus::IDLE){
				broadcastChannelIdleSignal(channelID);
			} else if(tmp->GetChipStatus(tmp->Get_chip(channelID, chipID)) == ChipStatus::IDLE){
				broadcastChipIdleSignal(tmp->Get_chip(channelID, chipID));
			}
		}
		delete transaction;//This transaction has been consumed and no more needed
	}

	void NVM_PHY_ONFI::ConnectToChannelIdleSignal(ChannelIdleHandlerType function)
	{
		connectedChannelIdleHandlers.push_back(function);
	}

	void NVM_PHY_ONFI::broadcastChannelIdleSignal(flash_channel_ID_type channelID)
	{
		for (std::vector<ChannelIdleHandlerType>::iterator it = connectedChannelIdleHandlers.begin();
			it != connectedChannelIdleHandlers.end(); it++) {
			(*it)(channelID);
		}
	}

	void NVM_PHY_ONFI::ConnectToChipIdleSignal(ChipIdleHandlerType function)
	{
		connectedChipIdleHandlers.push_back(function);
	}

	void NVM_PHY_ONFI::broadcastChipIdleSignal(NVM::FlashMemory::Flash_Chip* chip)
	{
		for (std::vector<ChipIdleHandlerType>::iterator it = connectedChipIdleHandlers.begin();
			it != connectedChipIdleHandlers.end(); it++) {
			(*it)(chip);
		}
	}
}