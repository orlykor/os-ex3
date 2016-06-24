#ifndef MAPREDUCEFRAMEWORK_H
#define MAPREDUCEFRAMEWORK_H

#include "MapReduceClient.h"
#include <utility>

typedef std::pair<k1Base*, v1Base*> IN_ITEM;
typedef std::pair<k3Base*, v3Base*> OUT_ITEM;

typedef std::list<IN_ITEM> IN_ITEMS_LIST; 
typedef std::list<OUT_ITEM> OUT_ITEMS_LIST;

OUT_ITEMS_LIST runMapReduceFramework(MapReduceBase& mapReduce, IN_ITEMS_LIST& itemsList, int multiThreadLevel);

void Emit2 (k2Base*, v2Base*);
void Emit3 (k3Base*, v3Base*);

#endif //MAPREDUCEFRAMEWORK_H
