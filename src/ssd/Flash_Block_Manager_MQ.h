#ifndef FLASH_BLOCK_MANAGER_MQ_H
#define FLASH_BLOCK_MANAGER_MQ_H

#include <set>
#include "Log_Update_Interval.h"
#include "NVM_Transaction_Flash_ER.h"
#include "MQ_types.h"



namespace SSD_Components
{


    enum class MQ_Block_Status {IDLE, WORKING, ERASING};

    class FTL;

    class Block_Type {
    public:
        Block_Type(NVM::FlashMemory::Physical_Page_Address& blockAddr);
        ~Block_Type();

        NVM::FlashMemory::Physical_Page_Address* blockAddr;

        MQ_Block_Status status = MQ_Block_Status::IDLE;
        lui_timestamp createTimestamp = 0;
        stream_id_type Stream_id = NO_STREAM;
        bool Holds_mapping_data = false;

        flash_page_ID_type currentPageIdx = 0;
        uint32_t invalid_page_count = 0;
        static uint32_t page_vector_size;
        uint64_t* invalid_page_bitmap = 0;
        int Ongoing_user_read_count = 0;
        int Ongoing_user_program_count = 0;
        
        // used for erase operation.
        NVM_Transaction_Flash_ER* Erase_transaction = NULL;

        // used by indicating group number of block after pushing.
        level_type prevQueue = UNDEFINED_LEVEL;

        // used by indicating group number of data moved to.
        level_type nextQueue = UNDEFINED_LEVEL;

        void SetEraseTags(level_type nextQueue);
        
        void SetUp(level_type prevQueue);
        void StartUsing(lui_timestamp createTimestamp, stream_id_type streamID, bool Holds_mapping_data);
    };

    class Block_Queue{
    public:
        Block_Queue(level_type level);
        ~Block_Queue();
        std::vector<Block_Type*> blockList;
        uint32_t currentBlockIdx;

        Block_Type* getCurrentBlock();
        void enqueueBlock(Block_Type* block);
        level_type getLevel();
        bool isFull();
    private:
        level_type level;
    };


	class Flash_Block_Manager_MQ
	{
	public:
		Flash_Block_Manager_MQ(FTL* ftl, uint32_t channelCount, uint32_t chipsPerChannel, uint32_t diesPerChip, uint32_t planesPerDie, uint32_t blocksPerPlane, uint32_t pagesPerBlock);
		~Flash_Block_Manager_MQ();
	
        void Allocate_page(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address& address, LPA_type lpa, uint32_t& level);
        void Allocate_mapping_page(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address& address);
        
        void Read_transaction_issued(const PPA_type& ppa);
        void Read_transaction_serviced(const PPA_type& ppa);
        void Program_transaction_serviced(const PPA_type& ppa);

        void Invalidate_page_in_block(const stream_id_type streamID, const PPA_type& ppa);
        bool isPageValid(const Block_Type* block, flash_page_ID_type page);

        Block_Type* getBlock(const PPA_type& ppa);

        void finishErase(Block_Type* block);
        bool Stop_servicing_writes(level_type level);

        bool isLastQueue(level_type level);

        void startGroupConfiguration();

        // Only executed when the pages are invalidated.
        void handleHotFilter(const LPA_type& lpa, const PPA_type& old_ppa, const bool forGC);
        bool isHot(const LPA_type& lpa);

        void handleLUIBlockAge(Block_Type* block);
    private:
        Log_Update_Interval* lui;
        FTL* ftl;
        uint32_t pagesPerBlock;
        std::vector<Block_Queue*> queues;
        std::vector<Block_Type*> blocks;

	    void Program_transaction_issued(Block_Type* block);
        void createQueue();
        void removeLastQueue();
	};
}

#endif // !FLASH_BLOCK_MANAGER_MQ_H
