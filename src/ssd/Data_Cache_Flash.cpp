#include "Data_Cache_Flash.h"
#include <assert.h>


namespace SSD_Components
{
	Data_Cache_Flash::Data_Cache_Flash(unsigned int capacity_in_pages, unsigned int RC_bound)
	 : capacity_in_pages(capacity_in_pages), RC_bound(RC_bound) {
		for(int i = 0; i < RC_bound; i++){
			subCapacity += i; 
			read_count_list.push_back(std::list<Data_Cache_Slot_Type*>());
		}

	}
	bool Data_Cache_Flash::Exists(const stream_id_type stream_id, const LPA_type lpn)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(stream_id, lpn);
		auto it = slots.find(key);
		if (it == slots.end()) {
			return false;
		}
		return true;
	}

	Data_Cache_Flash::~Data_Cache_Flash()
	{
		for (auto &slot : slots) {
			delete slot.second;
		}
	}

	Data_Cache_Slot_Type Data_Cache_Flash::Get_slot(const stream_id_type stream_id, const LPA_type lpn)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(stream_id, lpn);
		auto it = slots.find(key);
		assert(it != slots.end());
		RC_Increase_access_count(it->second);
		return *(it->second);
	}

	bool Data_Cache_Flash::Check_free_slot_availability()
	{
		return slots.size() < capacity_in_pages;
	}

	bool Data_Cache_Flash::Check_free_slot_availability(unsigned int no_of_slots)
	{
		return slots.size() + no_of_slots <= capacity_in_pages;
	}

	bool Data_Cache_Flash::Empty()
	{
		return slots.size() == 0;
	}

	bool Data_Cache_Flash::Full()
	{
		return slots.size() == capacity_in_pages;
	}

	Data_Cache_Slot_Type Data_Cache_Flash::Evict_one_slot_lru()
	{
		assert(slots.size() > 0);
		std::list<Data_Cache_Slot_Type*>& list_to_evict = read_count_list.at(0);
		assert(list_to_evict.size() != 0);
		Data_Cache_Slot_Type evicted_item = *list_to_evict.back();
		LPA_type key = evicted_item.LPA;
		slots.erase(key);
		delete list_to_evict.back();
		list_to_evict.pop_back();
		return evicted_item;
	}

	void Data_Cache_Flash::Change_slot_status_to_writeback(const stream_id_type stream_id, const LPA_type lpn)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(stream_id, lpn);
		auto it = slots.find(key);
		assert(it != slots.end());
		it->second->Status = Cache_Slot_Status::DIRTY_FLASH_WRITEBACK;
	}

	void Data_Cache_Flash::Insert_read_data(const stream_id_type stream_id, const LPA_type lpn, const data_cache_content_type content,
		const data_timestamp_type timestamp, const page_status_type state_bitmap_of_read_sectors)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(stream_id, lpn);
		
		if (slots.find(key) != slots.end()) {
			throw std::logic_error("Duplicate lpn insertion into data cache!");
		}
		if (slots.size() >= capacity_in_pages) {
			throw std::logic_error("Data cache overfull!");
		}

		Data_Cache_Slot_Type* cache_slot = new Data_Cache_Slot_Type();
		cache_slot->LPA = lpn;
		cache_slot->State_bitmap_of_existing_sectors = state_bitmap_of_read_sectors;
		cache_slot->Content = content;
		cache_slot->Timestamp = timestamp;
		cache_slot->Status = Cache_Slot_Status::CLEAN;

		slots[key] = cache_slot;
		RC_Insert_Data(cache_slot);
	}

	void Data_Cache_Flash::Insert_write_data(const stream_id_type stream_id, const LPA_type lpn, const data_cache_content_type content,
		const data_timestamp_type timestamp, const page_status_type state_bitmap_of_write_sectors)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(stream_id, lpn);
		if (slots.find(key) != slots.end()) {
			throw std::logic_error("Duplicate lpn insertion into data cache!!");
		}
		
		if (slots.size() >= capacity_in_pages) {
			throw std::logic_error("Data cache overfull!");
		}

		Data_Cache_Slot_Type* cache_slot = new Data_Cache_Slot_Type();
		cache_slot->LPA = lpn;
		cache_slot->State_bitmap_of_existing_sectors = state_bitmap_of_write_sectors;
		cache_slot->Content = content;
		cache_slot->Timestamp = timestamp;
		cache_slot->Status = Cache_Slot_Status::DIRTY_NO_FLASH_WRITEBACK;
		cache_slot->accessCount = 0;

		slots[key] = cache_slot;
		RC_Insert_Data(cache_slot);
	}

	void Data_Cache_Flash::Update_data(const stream_id_type stream_id, const LPA_type lpn, const data_cache_content_type content,
		const data_timestamp_type timestamp, const page_status_type state_bitmap_of_write_sectors)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(stream_id, lpn);
		auto it = slots.find(key);
		assert(it != slots.end());
		it->second->LPA = lpn;
		it->second->State_bitmap_of_existing_sectors = state_bitmap_of_write_sectors;
		it->second->Content = content;
		it->second->Timestamp = timestamp;
		it->second->Status = Cache_Slot_Status::DIRTY_NO_FLASH_WRITEBACK;

		RC_Increase_access_count(it->second);
	}

    void Data_Cache_Flash::RC_Insert_Data(Data_Cache_Slot_Type *slot)
    {
		std::list<Data_Cache_Slot_Type*>& lowestList = read_count_list.at(0);
		uint32_t i = 0;
		while(lowestList.size() == RC_getListCapacity(i) && i < RC_bound){
			i++;
			lowestList = read_count_list.at(i);
		}
		if(i == RC_bound){
			i = 0;
			lowestList = read_count_list.at(0);
		}
		slot->accessCount = i;
		lowestList.push_front(slot);
		slot->lru_list_ptr = lowestList.begin();
    }

    void Data_Cache_Flash::RC_Increase_access_count(Data_Cache_Slot_Type *slot)
    {
		std::list<Data_Cache_Slot_Type*>& curList = read_count_list.at(slot->accessCount);
		if(slot->accessCount == RC_bound - 1){
			curList.splice(curList.begin(), curList, slot->lru_list_ptr);
		} else{
			curList.erase(slot->lru_list_ptr);

			slot->accessCount += 1;
			std::list<Data_Cache_Slot_Type*>& upperList = read_count_list.at(slot->accessCount);
			upperList.push_front(slot);
			slot->lru_list_ptr = upperList.begin();

			if(upperList.size() >= RC_getListCapacity(slot->accessCount)){
				RC_Decrease_access_count(slot->accessCount);
			}
		}
    }

    void Data_Cache_Flash::RC_Decrease_access_count(const uint32_t accessCount)
    {
		assert(accessCount != 0);
		std::list<Data_Cache_Slot_Type*>& curList = read_count_list.at(accessCount);
		std::list<Data_Cache_Slot_Type*>& lowerList = read_count_list.at(accessCount - 1);

		Data_Cache_Slot_Type* dataToDecrease = curList.back();
		curList.pop_back();
		lowerList.push_front(dataToDecrease);
		dataToDecrease->lru_list_ptr = lowerList.begin();
		dataToDecrease->accessCount -= 1;
    }

    uint32_t Data_Cache_Flash::RC_getListCapacity(const uint32_t accessCount)
    {
		return capacity_in_pages / subCapacity * (RC_bound - accessCount);
    }

    void Data_Cache_Flash::Remove_slot(const stream_id_type stream_id, const LPA_type lpn)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(stream_id, lpn);
		auto it = slots.find(key);
		assert(it != slots.end());
		std::list<Data_Cache_Slot_Type*>& curList = read_count_list.at(it->second->accessCount);
		curList.erase(it->second->lru_list_ptr);
		delete it->second;
		slots.erase(key);
	}
}
