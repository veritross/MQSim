#include "Data_Cache_Flash.h"
#include <assert.h>


namespace SSD_Components
{
	Data_Cache_Flash::Data_Cache_Flash(unsigned int capacity_in_pages, bool LFU, unsigned int RC_bound)
	 : capacity_in_pages(capacity_in_pages), LFU(LFU), RC_bound(RC_bound), RC_capacity_in_pages(1024) {
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
		for(auto &list : lfu_list){
			delete list;
		}
	}

	Data_Cache_Slot_Type Data_Cache_Flash::Get_slot(const stream_id_type stream_id, const LPA_type lpn)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(stream_id, lpn);
		auto it = slots.find(key);
		assert(it != slots.end());
		if(LFU){
			LFU_Increase_access_count(it->second, key);
		}
		else{
			if (lru_list.begin()->first != key) {
				lru_list.splice(lru_list.begin(), lru_list, it->second->lru_list_ptr);
			}
		}

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

	//현재로써는 사용하지 않음.
	Data_Cache_Slot_Type Data_Cache_Flash::Evict_one_dirty_slot()
	{
		assert(slots.size() > 0);
		if(LFU){
			
		}
		else{
		}
		auto itr = lru_list.rbegin();
		while (itr != lru_list.rend()) {
			if ((*itr).second->Status == Cache_Slot_Status::DIRTY_NO_FLASH_WRITEBACK) {
				break;
			}
			itr++;
		}

		Data_Cache_Slot_Type evicted_item = *lru_list.back().second;
		if (itr == lru_list.rend()) {
			evicted_item.Status = Cache_Slot_Status::EMPTY;
			return evicted_item;
		}

		slots.erase(lru_list.back().first);
		delete lru_list.back().second;
		lru_list.pop_back();
		return evicted_item;
		
	}

	Data_Cache_Slot_Type Data_Cache_Flash::Evict_one_slot_lru()
	{
		assert(slots.size() > 0);
		if(LFU){
			std::list<std::pair<LPA_type, Data_Cache_Slot_Type*>>::iterator evicted_item_ptr = (*lfu_list.begin())->begin();
			Data_Cache_Slot_Type evicted_item = *evicted_item_ptr->second;
			LPA_type key = evicted_item_ptr->first;
			slots.erase(key);
			delete (*evicted_item_ptr).second;
			LFU_Remove_Data((*evicted_item_ptr).second, key);
			return evicted_item;
		}
		else{
			LPA_type key = lru_list.back().first;
			slots.erase(lru_list.back().first);
			Data_Cache_Slot_Type evicted_item = *lru_list.back().second;
			delete lru_list.back().second;
			lru_list.pop_back();
			return evicted_item;
		}
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
		cache_slot->accessCount = 0;
		if(LFU){
			LFU_Insert_Data(cache_slot, key);
		}
		else{
			lru_list.push_front(std::pair<LPA_type, Data_Cache_Slot_Type*>(key, cache_slot));
			cache_slot->lru_list_ptr = lru_list.begin();
		}
		slots[key] = cache_slot;
		RC_Remove_Data(stream_id, lpn);

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
		if(LFU){
			LFU_Insert_Data(cache_slot, key);
		}
		else{
			lru_list.push_front(std::pair<LPA_type, Data_Cache_Slot_Type*>(key, cache_slot));
			cache_slot->lru_list_ptr = lru_list.begin();
		}
		slots[key] = cache_slot;
		RC_Remove_Data(stream_id, lpn);
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
		if(LFU){
			LFU_Increase_access_count(it->second, key);
		}
		else{
			if (lru_list.begin()->first != key) {
				lru_list.splice(lru_list.begin(), lru_list, it->second->lru_list_ptr);
			}
		}
	}

    void Data_Cache_Flash::LFU_Increase_access_count(Data_Cache_Slot_Type* slot, LPA_type key)
    {
		auto listIt = slot->lfu_list_ptr;
		auto listItNext = listIt;
		listItNext++;
		slot->accessCount++;
		if(listItNext != lfu_list.end()){
			LFU_Remove_Data(slot, key);
			if((*listItNext)->front().second->accessCount == slot->accessCount){
				(*listItNext)->push_back(std::pair<LPA_type, Data_Cache_Slot_Type*>(key, slot));
				(slot)->lfu_list_ptr = listItNext;
			}
			else{
				auto a = new std::list<std::pair<LPA_type, Data_Cache_Slot_Type*>>();
				a->push_back(std::pair<LPA_type, Data_Cache_Slot_Type*>(key, slot));
				(slot)->lfu_list_ptr = lfu_list.insert(listItNext, a);
			}
		}
    }

    void Data_Cache_Flash::LFU_Insert_Data(Data_Cache_Slot_Type *slot, LPA_type key)
    {
		if(lfu_list.size() != 0 && lfu_list.front()->front().second->accessCount == slot->accessCount){
			lfu_list.front()->push_back(std::pair<LPA_type, Data_Cache_Slot_Type*>(key, slot));
		}
		else{
			std::list<std::pair<LPA_type, Data_Cache_Slot_Type*>>* newList = new std::list<std::pair<LPA_type, Data_Cache_Slot_Type*>>();
			newList->push_front(std::pair<LPA_type, Data_Cache_Slot_Type*>(key, slot));
			lfu_list.push_front(newList);
		}
		slot->lfu_list_ptr = lfu_list.begin();
    }

    void Data_Cache_Flash::LFU_Remove_Data(Data_Cache_Slot_Type *slot, LPA_type key)
    {
		auto LFU_list_of_slot = (*slot->lfu_list_ptr);

		auto dest_slot = LFU_list_of_slot->begin();
		while(dest_slot != LFU_list_of_slot->end()){
			if(dest_slot->first == key) break;
			dest_slot++;
		}
		assert(dest_slot != LFU_list_of_slot->end());
		LFU_list_of_slot->erase(dest_slot);
		if(LFU_list_of_slot->size() == 0){
			lfu_list.erase(slot->lfu_list_ptr);
		}
    }

    void Data_Cache_Flash::RC_Increase_access_count(const stream_id_type stream_id, const LPA_type lpn)
    {
		if(RC_bound == 0) return;
		LPA_type key = LPN_TO_UNIQUE_KEY(stream_id, lpn);
		auto it = std::find_if(read_count.begin(), read_count.end(),
			[&](const std::pair<LPA_type, int> v){
				return v.first == key;
			});
		if(it == read_count.end()){
			if(read_count.size() >= RC_capacity_in_pages){
				read_count.pop_back();
				read_count.push_front(std::pair<LPA_type, int>(key, 1));
			}
		}else{
			it->second++;
		}
    }

	//This function is executed when data that has parameter insert to cache.
    void Data_Cache_Flash::RC_Remove_Data(const stream_id_type stream_id, const LPA_type lpn)
    {
		if(RC_bound == 0) return;

		LPA_type key = LPN_TO_UNIQUE_KEY(stream_id, lpn);
		auto it = std::find_if(read_count.begin(), read_count.end(),
			[&](const std::pair<LPA_type, int> v){
				return v.first == key;
			});
		if(it != read_count.end()){
			read_count.erase(it);
		}
    }

	//return this data should caching or not.
    bool Data_Cache_Flash::RC_Compare_Data(const stream_id_type stream_id, const LPA_type lpn)
    {
		if(RC_bound == 0) return true;
		LPA_type key = LPN_TO_UNIQUE_KEY(stream_id, lpn);
		auto it = std::find_if(read_count.begin(), read_count.end(),
			[&](const std::pair<LPA_type, int> v){
				return v.first == key;
			});
		assert(it != read_count.end());
		return (*it).second >= RC_bound;
    }

    void Data_Cache_Flash::Remove_slot(const stream_id_type stream_id, const LPA_type lpn)
	{
		LPA_type key = LPN_TO_UNIQUE_KEY(stream_id, lpn);
		auto it = slots.find(key);
		assert(it != slots.end());
		if(LFU){
			LFU_Remove_Data(it->second, key);
		}else{
			lru_list.erase(it->second->lru_list_ptr);
		}
		delete it->second;
		slots.erase(key);
	}
}
