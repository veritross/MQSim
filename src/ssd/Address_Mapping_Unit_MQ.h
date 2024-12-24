#ifndef AMU_MQ_H
#define AMU_MQ_H

#include "Engine.h"
#include "NVM_Transaction_Flash_WR.h"
#include "MQ_types.h"
#include <set>

// Multi Queue using only 1-stream.

namespace SSD_Components
{
	class FTL;
	class NVM_PHY_ONFI;
	class Log_Update_Interval;
	class Flash_Block_Manager_MQ;
	class AddressMappingDomain;
	class Block_Type;
	class Block_Queue;

	typedef uint32_t MVPN_type;
	typedef uint32_t MPPN_type;

	class Address_Mapping_Unit_MQ: public MQSimEngine::Sim_Object
	{

		friend class MQ_GC_Unit;
	public:
		Address_Mapping_Unit_MQ(const sim_object_id_type& id, FTL* ftl, NVM_PHY_ONFI* flash_controller, Flash_Block_Manager_MQ* block_manager,
			bool ideal_mapping_table, unsigned int cmt_capacity_in_byte, 
			unsigned int ConcurrentStreamNo,
			unsigned int ChannelCount, unsigned int chip_no_per_channel, unsigned int DieNoPerChip, unsigned int PlaneNoPerDie,
			std::vector<std::vector<flash_channel_ID_type>> stream_channel_ids, std::vector<std::vector<flash_chip_ID_type>> stream_chip_ids,
			std::vector<std::vector<flash_die_ID_type>> stream_die_ids, std::vector<std::vector<flash_plane_ID_type>> stream_plane_ids,
			unsigned int Block_no_per_plane, unsigned int Page_no_per_block, unsigned int SectorsPerPage, unsigned int PageSizeInBytes);
		~Address_Mapping_Unit_MQ();
		void Setup_triggers();
		void Start_simulation();
		void Validate_simulation_config();
		void Execute_simulator_event(MQSimEngine::Sim_Event*);
		
		void Translate_lpa_to_ppa_and_dispatch(const std::list<NVM_Transaction*>& transactionList);

		void Get_data_mapping_info_for_gc(const stream_id_type stream_id, const LPA_type lpa, PPA_type& ppa, page_status_type& page_state);
		void Get_translation_mapping_info_for_gc(const stream_id_type stream_id, const MVPN_type mvpn, MPPN_type& mppa, sim_time_type& timestamp);
		PPA_type Convert_address_to_ppa(const NVM::FlashMemory::Physical_Page_Address& pageAddress);
		void Convert_ppa_to_address(const PPA_type ppa, NVM::FlashMemory::Physical_Page_Address& addr);
		
		void Set_barrier_for_accessing_physical_block(const Block_Type* block);//At the very beginning of executing a GC request, the GC target physical block (that is selected for erase) should be protected by a barrier. The LPAs within this block are unknown until the content of the physical pages within the block are read one-by-one. Therfore, at the start of the GC execution, the barrier is set for the physical block. Later, when the LPAs are read from the physical block, the above functions are used to lock each of the LPAs.
		void Start_servicing_writes_for_level(const uint32_t level);//This function is invoked when GC execution is finished on a plane and the plane has enough number of free pages to service writes
		
		typedef void(*TransactionServicedSignalHandlerType) (NVM_Transaction_Flash*);
		void Connect_to_user_request_arrived_signal(TransactionServicedSignalHandlerType function)
		{
			connected_transaction_serviced_signal_handler = function;
		}

		Block_Type* selectVictimBlockCB(Block_Queue* queue);

	private:
		void Set_barrier_for_accessing_lpa(const stream_id_type stream_id, const LPA_type lpa); //It sets a barrier for accessing an LPA, when the GC unit (i.e., GC_and_WL_Unit_Base) starts moving an LPA from one physical page to another physical page. This type of barrier is pretty much like a memory barrier in CPU, i.e., all accesses to the lpa that issued before setting the barrier still can be executed, but no new access is allowed.
		void Set_barrier_for_accessing_mvpn(const stream_id_type stream_id, const MVPN_type mvpn); //It sets a barrier for accessing an MVPN, when the GC unit(i.e., GC_and_WL_Unit_Base) starts moving an mvpn from one physical page to another physical page. This type of barrier is pretty much like a memory barrier in CPU, i.e., all accesses to the lpa that issued before setting the barrier can be executed, but no new access is allowed.
		void Remove_barrier_for_accessing_lpa(const stream_id_type stream_id, const LPA_type lpa); //Removes the barrier that has already been set for accessing an LPA (i.e., the GC_and_WL_Unit_Base unit successfully finished relocating LPA from one physical location to another physical location).
		void Remove_barrier_for_accessing_mvpn(const stream_id_type stream_id, const MVPN_type mvpn); //Removes the barrier that has already been set for accessing an MVPN (i.e., the GC_and_WL_Unit_Base unit successfully finished relocating MVPN from one physical location to another physical location).
        
		static Address_Mapping_Unit_MQ* _my_instance;
		FTL* ftl;
		NVM_PHY_ONFI* flash_controller;
		Flash_Block_Manager_MQ* block_manager;
		AddressMappingDomain** domains;
		bool ideal_mapping_table;
		TransactionServicedSignalHandlerType connected_transaction_serviced_signal_handler;

		std::vector<std::set<NVM_Transaction_Flash_WR*>> write_transactions_for_level;

		PPA_type online_create_entry_for_reads(LPA_type lpa, const stream_id_type stream_id, NVM::FlashMemory::Physical_Page_Address& read_address, uint64_t read_sectors_bitmap);

		bool query_cmt(NVM_Transaction_Flash* tr);

		void insertUserTrBarrierQueue(NVM_Transaction_Flash* transaction);
		void insertMappingTrBarrierQueue(stream_id_type stream_id, MVPN_type mvpn, bool read);
		bool is_lpa_locked_for_gc(stream_id_type stream_id, LPA_type lpa);
		bool is_mvpn_locked_for_gc(stream_id_type stream_id, MVPN_type mvpn);

        bool translate_lpa_to_ppa(stream_id_type streamID, NVM_Transaction_Flash* transaction);
        void allocate_page_for_write(NVM_Transaction_Flash_WR* tr);
		void manage_unsuccessful_transaction(NVM_Transaction_Flash_WR* tr);

		MVPN_type get_MVPN(const LPA_type lpa, const stream_id_type stream_id);
		LPA_type get_start_LPN_in_MVP(const MVPN_type mvpn);
		LPA_type get_end_LPN_in_MVP(const MVPN_type mvpn);


		//functions related to mapping.
		static void handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash* transaction);
		void generate_flash_writeback_request_for_mapping_data(const stream_id_type stream_id, const LPA_type lpn);
		void generate_flash_read_request_for_mapping_data(const stream_id_type stream_id, const LPA_type lpn);
		bool request_mapping_entry(const stream_id_type stream_id, const LPA_type lpa);
		void allocate_page_for_translation_write(NVM_Transaction_Flash_WR* tr, MVPN_type mvpn);

        uint32_t CMT_entry_size, GTD_entry_size, no_of_translation_entries_per_page;
		unsigned int no_of_input_streams;
        unsigned int channel_count;
        unsigned int chip_no_per_channel;
		unsigned int die_no_per_chip;
		unsigned int plane_no_per_die;
		unsigned int block_no_per_plane;
		unsigned int pages_no_per_block;
		unsigned int sector_no_per_page;

		unsigned int page_no_per_channel = 0;
		unsigned int page_no_per_chip = 0;
		unsigned int page_no_per_die = 0;
		unsigned int page_no_per_plane = 0;



	};
}

#endif //AMU_MQ_H