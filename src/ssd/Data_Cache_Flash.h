#ifndef DATA_CACHE_FLASH_H
#define DATA_CACHE_FLASH_H

#include <list>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include "../nvm_chip/flash_memory/FlashTypes.h"
#include "SSD_Defs.h"
#include "Data_Cache_Manager_Base.h"
#include "NVM_Transaction_Flash.h"

namespace SSD_Components
{
	enum class Cache_Slot_Status { EMPTY, CLEAN, DIRTY_NO_FLASH_WRITEBACK, DIRTY_FLASH_WRITEBACK };
	struct Data_Cache_Slot_Type
	{
		unsigned long long State_bitmap_of_existing_sectors;
		LPA_type LPA;
		data_cache_content_type Content;
		data_timestamp_type Timestamp;
		Cache_Slot_Status Status;
		std::list<std::pair<LPA_type, Data_Cache_Slot_Type*>>::iterator lru_list_ptr;//used for fast implementation of LRU
		std::list<std::list<std::pair<LPA_type, Data_Cache_Slot_Type*>>*>::iterator lfu_list_ptr;
		int accessCount;
	};

	enum class Data_Cache_Simulation_Event_Type {
		MEMORY_READ_FOR_CACHE_EVICTION_FINISHED,
		MEMORY_WRITE_FOR_CACHE_FINISHED,
		MEMORY_READ_FOR_USERIO_FINISHED,
		MEMORY_WRITE_FOR_USERIO_FINISHED
	};

	struct Memory_Transfer_Info
	{
		unsigned int Size_in_bytes;
		void* Related_request;
		Data_Cache_Simulation_Event_Type next_event_type;
		stream_id_type Stream_id;
	};

	class Data_Cache_Flash
	{
	public:
		Data_Cache_Flash(unsigned int capacity_in_pages = 0, bool LFU = false, unsigned int RC_bound = 0, unsigned int RC_Capacity = 0);
		~Data_Cache_Flash();
		bool Exists(const stream_id_type streamID, const LPA_type lpn);
		bool Check_free_slot_availability();
		bool Check_free_slot_availability(unsigned int no_of_slots);
		bool Empty();
		bool Full();
		Data_Cache_Slot_Type Get_slot(const stream_id_type stream_id, const LPA_type lpn);
		Data_Cache_Slot_Type Evict_one_dirty_slot();
		Data_Cache_Slot_Type Evict_one_slot_lru();
		void Change_slot_status_to_writeback(const stream_id_type stream_id, const LPA_type lpn);
		void Remove_slot(const stream_id_type stream_id, const LPA_type lpn);
		void Insert_read_data(const stream_id_type stream_id, const LPA_type lpn, const data_cache_content_type content, const data_timestamp_type timestamp, const page_status_type state_bitmap_of_read_sectors);
		void Insert_write_data(const stream_id_type stream_id, const LPA_type lpn, const data_cache_content_type content, const data_timestamp_type timestamp, const page_status_type state_bitmap_of_write_sectors);
		void Update_data(const stream_id_type stream_id, const LPA_type lpn, const data_cache_content_type content, const data_timestamp_type timestamp, const page_status_type state_bitmap_of_write_sectors);
		void LFU_Increase_access_count(Data_Cache_Slot_Type* slot, LPA_type key);
		void LFU_Insert_Data(Data_Cache_Slot_Type* slot, LPA_type key);
		void LFU_Remove_Data(Data_Cache_Slot_Type* slot, LPA_type key);
		void RC_Increase_access_count(const stream_id_type stream_id, const LPA_type lpn);
		void RC_Remove_Data(const stream_id_type stream_id, const LPA_type lpn);
		bool RC_Compare_Data(const stream_id_type stream_id, const LPA_type lpn);
	private:
		std::unordered_map<LPA_type, Data_Cache_Slot_Type*> slots;
		std::list<std::pair<LPA_type, Data_Cache_Slot_Type*>> lru_list;
		std::list<std::list<std::pair<LPA_type, Data_Cache_Slot_Type*>>*> lfu_list;
		std::list<std::pair<LPA_type, int>> read_count;
		const unsigned int RC_Capacity;
		const unsigned int RC_bound;
		unsigned int capacity_in_pages;
		bool LFU;
	};
}

#endif // !DATA_CACHE_FLASH_H
