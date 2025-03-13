#ifndef LOG_UPDATE_INTERVAL_MQ_H
#define LOG_UPDATE_INTERVAL_MQ_H

#include <map>
#include <vector>
#include "MQ_types.h"


namespace SSD_Components
{
	class Block_Type;


	class CON{
	private:
	public:
		// In pages unit.
		const static lui_timestamp GROUP_CONFIGURE_EPOCH;
		const static bool IS_ZERO(double v){
			return (v < 1e-9 && v > -1e-9);
		}
		const static uint32_t TIMETABLE_ENTRY_UNIT;
		const static bool ENTRY_VERIFY(LPA_type lba){
			return (lba % TIMETABLE_ENTRY_UNIT) == 0;
		}
		const static lui_timestamp TIMESTAMP_NOT_ACCESSED;
		const static uint64_t UPDATE_INTERVAL_TABLE_SIZE;
		const static uint8_t HOT_FILTER_BITS_COUNT;

		const static uint8_t NOT_NOTICIBLE_REDUCTION_THRESHOLD;
		const static double NOTICIBLE_REDUCTION_CRITERIA;
		const static double UID_SELECTION_THRESHOLD;
	};

	struct HotFilter{
		uint64_t* filter;
		uint32_t vectorCount = 0;

		HotFilter(uint64_t noOfBlocks);
		~HotFilter();
		void clearFilter();
		uint8_t getFilter(const LPA_type lpa);
		void setFilter(const LPA_type lpa, const uint8_t newBit);
	};

	class UID{
	private:
		//key. group count.
		//value. group size.

		double MarkovChain(const std::vector<double>& p, const double uninvalidatedRatio, double lastGroupWAF);
		double getLastGroupWAF(const std::map<uint64_t, uint64_t>& intervalCountTable, uint32_t pagesPerBlock, std::map<uint64_t, uint64_t>::const_iterator& lastGroupItr);
	public:
		UID();

		// Used only start of simulation.
		UID(const std::vector<uint32_t>& groupConf);

		std::vector<uint32_t> groupConf;
		double createUID(const std::map<uint64_t, uint64_t>& intervalCountTable, uint64_t totalReqs, uint32_t totalBlocksCount, uint32_t pagesPerBlock, double avgBlocksResTime);
		double getWAF(const std::map<uint64_t, uint64_t>& intervalCountTable, uint64_t totalReqs, double avgBlocksResTime, uint32_t pagesPerBlock);
	};


	class Log_Update_Interval 
	{
	private:
		uint64_t totalBlocksCount;
		uint32_t pagesPerBlock;
		
		lui_timestamp requestCountInCurrentInterval;
		lui_timestamp currentTimestamp;
		
		uint64_t totalErasedBlocksCount;
		uint64_t totalErasedBlocksResidentTime;

		
		//keeps track of the number of pages for specific update intervals.
		// Sampling rate is 0.01(one in every 100 blocks)
		std::map<uint64_t, uint64_t> updateIntervalTable;
		uint64_t sumOfUpdateIntervalTable;
		uint64_t totalHotReqs;
		uint64_t totalReqs;

		//records timestamps of page updates to compute the update intervals of data pages.
		std::map<uint64_t, lui_timestamp> timestampTable;
		
		void scheduleCurrentTimestamp();
		void setTables(const LPA_type lba);
		
		UID* currentUID;
		bool changeUIDTag;
		
		public:
		Log_Update_Interval(uint64_t totalBlocksCount, uint32_t pagesPerBlock, const std::vector<uint32_t>& initialGroupConf);
        ~Log_Update_Interval();
		
		HotFilter* hotFilter;
        bool isHot(const LPA_type lba);
		void updateHotFilter(const LPA_type lba, const lui_timestamp blkAge, const level_type level, const bool forGC);
		void updateTable(const LPA_type lba);
		void addBlockAge(const Block_Type* block, const Queue_Type queueType);
		
		void selectUID();
		void clearTable();

		UID* getUID();
		lui_timestamp getCurrentTimestamp();

	};
}

#endif // !LOG_UPDATE_INTERVAL_MQ_H
