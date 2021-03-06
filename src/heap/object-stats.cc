// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/object-stats.h"

#include "src/counters.h"
#include "src/heap/heap-inl.h"
#include "src/isolate.h"
#include "src/macro-assembler.h"
#include "src/utils.h"

namespace v8 {
namespace internal {

static base::LazyMutex object_stats_mutex = LAZY_MUTEX_INITIALIZER;


void ObjectStats::ClearObjectStats(bool clear_last_time_stats) {
  memset(object_counts_, 0, sizeof(object_counts_));
  memset(object_sizes_, 0, sizeof(object_sizes_));
  memset(over_allocated_, 0, sizeof(over_allocated_));
  memset(size_histogram_, 0, sizeof(size_histogram_));
  memset(over_allocated_histogram_, 0, sizeof(over_allocated_histogram_));
  if (clear_last_time_stats) {
    memset(object_counts_last_time_, 0, sizeof(object_counts_last_time_));
    memset(object_sizes_last_time_, 0, sizeof(object_sizes_last_time_));
  }
  visited_fixed_array_sub_types_.clear();
}

static void PrintJSONArray(size_t* array, const int len) {
  PrintF("[ ");
  for (int i = 0; i < len; i++) {
    PrintF("%zu", array[i]);
    if (i != (len - 1)) PrintF(", ");
  }
  PrintF(" ]");
}

void ObjectStats::PrintJSON(const char* key) {
  double time = isolate()->time_millis_since_init();
  int gc_count = heap()->gc_count();

#define PRINT_KEY_AND_ID()                                     \
  PrintF("\"isolate\": \"%p\", \"id\": %d, \"key\": \"%s\", ", \
         reinterpret_cast<void*>(isolate()), gc_count, key);

  // gc_descriptor
  PrintF("{ ");
  PRINT_KEY_AND_ID();
  PrintF("\"type\": \"gc_descriptor\", \"time\": %f }\n", time);
  // bucket_sizes
  PrintF("{ ");
  PRINT_KEY_AND_ID();
  PrintF("\"type\": \"bucket_sizes\", \"sizes\": [ ");
  for (int i = 0; i < kNumberOfBuckets; i++) {
    PrintF("%d", 1 << (kFirstBucketShift + i));
    if (i != (kNumberOfBuckets - 1)) PrintF(", ");
  }
  PrintF(" ] }\n");
// instance_type_data
#define PRINT_INSTANCE_TYPE_DATA(name, index)                         \
  PrintF("{ ");                                                       \
  PRINT_KEY_AND_ID();                                                 \
  PrintF("\"type\": \"instance_type_data\", ");                       \
  PrintF("\"instance_type\": %d, ", index);                           \
  PrintF("\"instance_type_name\": \"%s\", ", name);                   \
  PrintF("\"overall\": %zu, ", object_sizes_[index]);                 \
  PrintF("\"count\": %zu, ", object_counts_[index]);                  \
  PrintF("\"over_allocated\": %zu, ", over_allocated_[index]);        \
  PrintF("\"histogram\": ");                                          \
  PrintJSONArray(size_histogram_[index], kNumberOfBuckets);           \
  PrintF(",");                                                        \
  PrintF("\"over_allocated_histogram\": ");                           \
  PrintJSONArray(over_allocated_histogram_[index], kNumberOfBuckets); \
  PrintF(" }\n");

#define INSTANCE_TYPE_WRAPPER(name) PRINT_INSTANCE_TYPE_DATA(#name, name)
#define CODE_KIND_WRAPPER(name)            \
  PRINT_INSTANCE_TYPE_DATA("*CODE_" #name, \
                           FIRST_CODE_KIND_SUB_TYPE + Code::name)
#define FIXED_ARRAY_SUB_INSTANCE_TYPE_WRAPPER(name) \
  PRINT_INSTANCE_TYPE_DATA("*FIXED_ARRAY_" #name,   \
                           FIRST_FIXED_ARRAY_SUB_TYPE + name)
#define CODE_AGE_WRAPPER(name) \
  PRINT_INSTANCE_TYPE_DATA(    \
      "*CODE_AGE_" #name,      \
      FIRST_CODE_AGE_SUB_TYPE + Code::k##name##CodeAge - Code::kFirstCodeAge)

  INSTANCE_TYPE_LIST(INSTANCE_TYPE_WRAPPER)
  CODE_KIND_LIST(CODE_KIND_WRAPPER)
  FIXED_ARRAY_SUB_INSTANCE_TYPE_LIST(FIXED_ARRAY_SUB_INSTANCE_TYPE_WRAPPER)
  CODE_AGE_LIST_COMPLETE(CODE_AGE_WRAPPER)

#undef INSTANCE_TYPE_WRAPPER
#undef CODE_KIND_WRAPPER
#undef FIXED_ARRAY_SUB_INSTANCE_TYPE_WRAPPER
#undef CODE_AGE_WRAPPER
#undef PRINT_INSTANCE_TYPE_DATA
}

void ObjectStats::CheckpointObjectStats() {
  base::LockGuard<base::Mutex> lock_guard(object_stats_mutex.Pointer());
  Counters* counters = isolate()->counters();
#define ADJUST_LAST_TIME_OBJECT_COUNT(name)              \
  counters->count_of_##name()->Increment(                \
      static_cast<int>(object_counts_[name]));           \
  counters->count_of_##name()->Decrement(                \
      static_cast<int>(object_counts_last_time_[name])); \
  counters->size_of_##name()->Increment(                 \
      static_cast<int>(object_sizes_[name]));            \
  counters->size_of_##name()->Decrement(                 \
      static_cast<int>(object_sizes_last_time_[name]));
  INSTANCE_TYPE_LIST(ADJUST_LAST_TIME_OBJECT_COUNT)
#undef ADJUST_LAST_TIME_OBJECT_COUNT
  int index;
#define ADJUST_LAST_TIME_OBJECT_COUNT(name)               \
  index = FIRST_CODE_KIND_SUB_TYPE + Code::name;          \
  counters->count_of_CODE_TYPE_##name()->Increment(       \
      static_cast<int>(object_counts_[index]));           \
  counters->count_of_CODE_TYPE_##name()->Decrement(       \
      static_cast<int>(object_counts_last_time_[index])); \
  counters->size_of_CODE_TYPE_##name()->Increment(        \
      static_cast<int>(object_sizes_[index]));            \
  counters->size_of_CODE_TYPE_##name()->Decrement(        \
      static_cast<int>(object_sizes_last_time_[index]));
  CODE_KIND_LIST(ADJUST_LAST_TIME_OBJECT_COUNT)
#undef ADJUST_LAST_TIME_OBJECT_COUNT
#define ADJUST_LAST_TIME_OBJECT_COUNT(name)               \
  index = FIRST_FIXED_ARRAY_SUB_TYPE + name;              \
  counters->count_of_FIXED_ARRAY_##name()->Increment(     \
      static_cast<int>(object_counts_[index]));           \
  counters->count_of_FIXED_ARRAY_##name()->Decrement(     \
      static_cast<int>(object_counts_last_time_[index])); \
  counters->size_of_FIXED_ARRAY_##name()->Increment(      \
      static_cast<int>(object_sizes_[index]));            \
  counters->size_of_FIXED_ARRAY_##name()->Decrement(      \
      static_cast<int>(object_sizes_last_time_[index]));
  FIXED_ARRAY_SUB_INSTANCE_TYPE_LIST(ADJUST_LAST_TIME_OBJECT_COUNT)
#undef ADJUST_LAST_TIME_OBJECT_COUNT
#define ADJUST_LAST_TIME_OBJECT_COUNT(name)                                   \
  index =                                                                     \
      FIRST_CODE_AGE_SUB_TYPE + Code::k##name##CodeAge - Code::kFirstCodeAge; \
  counters->count_of_CODE_AGE_##name()->Increment(                            \
      static_cast<int>(object_counts_[index]));                               \
  counters->count_of_CODE_AGE_##name()->Decrement(                            \
      static_cast<int>(object_counts_last_time_[index]));                     \
  counters->size_of_CODE_AGE_##name()->Increment(                             \
      static_cast<int>(object_sizes_[index]));                                \
  counters->size_of_CODE_AGE_##name()->Decrement(                             \
      static_cast<int>(object_sizes_last_time_[index]));
  CODE_AGE_LIST_COMPLETE(ADJUST_LAST_TIME_OBJECT_COUNT)
#undef ADJUST_LAST_TIME_OBJECT_COUNT

  MemCopy(object_counts_last_time_, object_counts_, sizeof(object_counts_));
  MemCopy(object_sizes_last_time_, object_sizes_, sizeof(object_sizes_));
  ClearObjectStats();
}


Isolate* ObjectStats::isolate() { return heap()->isolate(); }

void ObjectStatsCollector::CollectStatistics(HeapObject* obj) {
  Map* map = obj->map();

  // Record for the InstanceType.
  int object_size = obj->Size();
  stats_->RecordObjectStats(map->instance_type(), object_size);

  // Record specific sub types where possible.
  if (obj->IsMap()) RecordMapDetails(Map::cast(obj));
  if (obj->IsCode()) RecordCodeDetails(Code::cast(obj));
  if (obj->IsSharedFunctionInfo()) {
    RecordSharedFunctionInfoDetails(SharedFunctionInfo::cast(obj));
  }
  if (obj->IsFixedArray()) RecordFixedArrayDetails(FixedArray::cast(obj));
  if (obj->IsJSObject()) RecordJSObjectDetails(JSObject::cast(obj));
  if (obj->IsJSWeakCollection()) {
    RecordJSWeakCollectionDetails(JSWeakCollection::cast(obj));
  }
  if (obj->IsJSCollection()) {
    RecordJSCollectionDetails(JSObject::cast(obj));
  }
  if (obj->IsJSFunction()) RecordJSFunctionDetails(JSFunction::cast(obj));
  if (obj->IsScript()) RecordScriptDetails(Script::cast(obj));
}

void ObjectStatsCollector::CollectGlobalStatistics() {
  // Global FixedArrays.
  RecordFixedArrayHelper(nullptr, heap_->weak_new_space_object_to_code_list(),
                         WEAK_NEW_SPACE_OBJECT_TO_CODE_SUB_TYPE, 0);
  RecordFixedArrayHelper(nullptr, heap_->serialized_templates(),
                         SERIALIZED_TEMPLATES_SUB_TYPE, 0);
  RecordFixedArrayHelper(nullptr, heap_->number_string_cache(),
                         NUMBER_STRING_CACHE_SUB_TYPE, 0);
  RecordFixedArrayHelper(nullptr, heap_->single_character_string_cache(),
                         SINGLE_CHARACTER_STRING_CACHE_SUB_TYPE, 0);
  RecordFixedArrayHelper(nullptr, heap_->string_split_cache(),
                         STRING_SPLIT_CACHE_SUB_TYPE, 0);
  RecordFixedArrayHelper(nullptr, heap_->regexp_multiple_cache(),
                         REGEXP_MULTIPLE_CACHE_SUB_TYPE, 0);
  RecordFixedArrayHelper(nullptr, heap_->retained_maps(),
                         RETAINED_MAPS_SUB_TYPE, 0);

  // Global weak FixedArrays.
  RecordFixedArrayHelper(
      nullptr, WeakFixedArray::cast(heap_->noscript_shared_function_infos()),
      NOSCRIPT_SHARED_FUNCTION_INFOS_SUB_TYPE, 0);
  RecordFixedArrayHelper(nullptr, WeakFixedArray::cast(heap_->script_list()),
                         SCRIPT_LIST_SUB_TYPE, 0);

  // Global hash tables.
  RecordHashTableHelper(nullptr, heap_->string_table(), STRING_TABLE_SUB_TYPE);
  RecordHashTableHelper(nullptr, heap_->weak_object_to_code_table(),
                        OBJECT_TO_CODE_SUB_TYPE);
  RecordHashTableHelper(nullptr, heap_->code_stubs(),
                        CODE_STUBS_TABLE_SUB_TYPE);
  RecordHashTableHelper(nullptr, heap_->intrinsic_function_names(),
                        INTRINSIC_FUNCTION_NAMES_SUB_TYPE);
  RecordHashTableHelper(nullptr, heap_->empty_properties_dictionary(),
                        EMPTY_PROPERTIES_DICTIONARY_SUB_TYPE);
}

static bool CanRecordFixedArray(Heap* heap, FixedArrayBase* array) {
  return array->map()->instance_type() == FIXED_ARRAY_TYPE &&
         array->map() != heap->fixed_double_array_map() &&
         array != heap->empty_fixed_array() &&
         array != heap->empty_byte_array() &&
         array != heap->empty_literals_array() &&
         array != heap->empty_sloppy_arguments_elements() &&
         array != heap->empty_slow_element_dictionary() &&
         array != heap->empty_descriptor_array() &&
         array != heap->empty_properties_dictionary();
}

static bool IsCowArray(Heap* heap, FixedArrayBase* array) {
  return array->map() == heap->fixed_cow_array_map();
}

static bool SameLiveness(HeapObject* obj1, HeapObject* obj2) {
  return obj1 == nullptr || obj2 == nullptr ||
         ObjectMarking::Color(obj1) == ObjectMarking::Color(obj2);
}

bool ObjectStatsCollector::RecordFixedArrayHelper(HeapObject* parent,
                                                  FixedArray* array,
                                                  int subtype,
                                                  size_t overhead) {
  if (SameLiveness(parent, array) && CanRecordFixedArray(heap_, array) &&
      !IsCowArray(heap_, array)) {
    return stats_->RecordFixedArraySubTypeStats(array, subtype, array->Size(),
                                                overhead);
  }
  return false;
}

void ObjectStatsCollector::RecursivelyRecordFixedArrayHelper(HeapObject* parent,
                                                             FixedArray* array,
                                                             int subtype) {
  if (RecordFixedArrayHelper(parent, array, subtype, 0)) {
    for (int i = 0; i < array->length(); i++) {
      if (array->get(i)->IsFixedArray()) {
        RecursivelyRecordFixedArrayHelper(
            parent, FixedArray::cast(array->get(i)), subtype);
      }
    }
  }
}

template <class HashTable>
void ObjectStatsCollector::RecordHashTableHelper(HeapObject* parent,
                                                 HashTable* array,
                                                 int subtype) {
  int used = array->NumberOfElements() * HashTable::kEntrySize * kPointerSize;
  CHECK_GE(array->Size(), used);
  size_t overhead = array->Size() - used -
                    HashTable::kElementsStartIndex * kPointerSize -
                    FixedArray::kHeaderSize;
  RecordFixedArrayHelper(parent, array, subtype, overhead);
}

void ObjectStatsCollector::RecordJSObjectDetails(JSObject* object) {
  size_t overhead = 0;
  FixedArrayBase* elements = object->elements();
  if (CanRecordFixedArray(heap_, elements) && !IsCowArray(heap_, elements)) {
    if (elements->IsDictionary() && SameLiveness(object, elements)) {
      SeededNumberDictionary* dict = SeededNumberDictionary::cast(elements);
      RecordHashTableHelper(object, dict, DICTIONARY_ELEMENTS_SUB_TYPE);
    } else {
      if (IsFastHoleyElementsKind(object->GetElementsKind())) {
        int used = object->GetFastElementsUsage() * kPointerSize;
        if (object->GetElementsKind() == FAST_HOLEY_DOUBLE_ELEMENTS) used *= 2;
        CHECK_GE(elements->Size(), used);
        overhead = elements->Size() - used - FixedArray::kHeaderSize;
      }
      stats_->RecordFixedArraySubTypeStats(elements, FAST_ELEMENTS_SUB_TYPE,
                                           elements->Size(), overhead);
    }
  }

  overhead = 0;
  FixedArrayBase* properties = object->properties();
  if (CanRecordFixedArray(heap_, properties) &&
      SameLiveness(object, properties) && !IsCowArray(heap_, properties)) {
    if (properties->IsDictionary()) {
      NameDictionary* dict = NameDictionary::cast(properties);
      RecordHashTableHelper(object, dict, DICTIONARY_PROPERTIES_SUB_TYPE);
    } else {
      stats_->RecordFixedArraySubTypeStats(properties, FAST_PROPERTIES_SUB_TYPE,
                                           properties->Size(), overhead);
    }
  }
}

void ObjectStatsCollector::RecordJSWeakCollectionDetails(
    JSWeakCollection* obj) {
  if (obj->table()->IsHashTable()) {
    ObjectHashTable* table = ObjectHashTable::cast(obj->table());
    int used = table->NumberOfElements() * ObjectHashTable::kEntrySize;
    size_t overhead = table->Size() - used;
    RecordFixedArrayHelper(obj, table, JS_WEAK_COLLECTION_SUB_TYPE, overhead);
  }
}

void ObjectStatsCollector::RecordJSCollectionDetails(JSObject* obj) {
  // The JS versions use a different HashTable implementation that cannot use
  // the regular helper. Since overall impact is usually small just record
  // without overhead.
  if (obj->IsJSMap()) {
    RecordFixedArrayHelper(nullptr, FixedArray::cast(JSMap::cast(obj)->table()),
                           JS_COLLECTION_SUB_TYPE, 0);
  }
  if (obj->IsJSSet()) {
    RecordFixedArrayHelper(nullptr, FixedArray::cast(JSSet::cast(obj)->table()),
                           JS_COLLECTION_SUB_TYPE, 0);
  }
}

void ObjectStatsCollector::RecordScriptDetails(Script* obj) {
  Object* infos = WeakFixedArray::cast(obj->shared_function_infos());
  if (infos->IsWeakFixedArray())
    RecordFixedArrayHelper(obj, WeakFixedArray::cast(infos),
                           SHARED_FUNCTION_INFOS_SUB_TYPE, 0);
}

void ObjectStatsCollector::RecordMapDetails(Map* map_obj) {
  DescriptorArray* array = map_obj->instance_descriptors();
  if (map_obj->owns_descriptors() && array != heap_->empty_descriptor_array() &&
      SameLiveness(map_obj, array)) {
    RecordFixedArrayHelper(map_obj, array, DESCRIPTOR_ARRAY_SUB_TYPE, 0);
    if (array->HasEnumCache()) {
      RecordFixedArrayHelper(array, array->GetEnumCache(), ENUM_CACHE_SUB_TYPE,
                             0);
    }
    if (array->HasEnumIndicesCache()) {
      RecordFixedArrayHelper(array, array->GetEnumIndicesCache(),
                             ENUM_INDICES_CACHE_SUB_TYPE, 0);
    }
  }

  if (map_obj->has_code_cache()) {
    RecordFixedArrayHelper(map_obj, map_obj->code_cache(),
                           MAP_CODE_CACHE_SUB_TYPE, 0);
  }

  for (DependentCode* cur_dependent_code = map_obj->dependent_code();
       cur_dependent_code != heap_->empty_fixed_array();
       cur_dependent_code = DependentCode::cast(
           cur_dependent_code->get(DependentCode::kNextLinkIndex))) {
    RecordFixedArrayHelper(map_obj, cur_dependent_code, DEPENDENT_CODE_SUB_TYPE,
                           0);
  }

  if (map_obj->is_prototype_map()) {
    if (map_obj->prototype_info()->IsPrototypeInfo()) {
      PrototypeInfo* info = PrototypeInfo::cast(map_obj->prototype_info());
      Object* users = info->prototype_users();
      if (users->IsWeakFixedArray()) {
        RecordFixedArrayHelper(map_obj, WeakFixedArray::cast(users),
                               PROTOTYPE_USERS_SUB_TYPE, 0);
      }
    }
  }
}

void ObjectStatsCollector::RecordCodeDetails(Code* code) {
  stats_->RecordCodeSubTypeStats(code->kind(), code->GetAge(), code->Size());
  RecordFixedArrayHelper(code, code->deoptimization_data(),
                         DEOPTIMIZATION_DATA_SUB_TYPE, 0);
  RecordFixedArrayHelper(code, code->handler_table(), HANDLER_TABLE_SUB_TYPE,
                         0);
  int const mode_mask = RelocInfo::ModeMask(RelocInfo::EMBEDDED_OBJECT);
  for (RelocIterator it(code, mode_mask); !it.done(); it.next()) {
    RelocInfo::Mode mode = it.rinfo()->rmode();
    if (mode == RelocInfo::EMBEDDED_OBJECT) {
      Object* target = it.rinfo()->target_object();
      if (target->IsFixedArray()) {
        RecursivelyRecordFixedArrayHelper(code, FixedArray::cast(target),
                                          EMBEDDED_OBJECT_SUB_TYPE);
      }
    }
  }
}

void ObjectStatsCollector::RecordSharedFunctionInfoDetails(
    SharedFunctionInfo* sfi) {
  FixedArray* scope_info = sfi->scope_info();
  RecordFixedArrayHelper(sfi, scope_info, SCOPE_INFO_SUB_TYPE, 0);
  TypeFeedbackMetadata* feedback_metadata = sfi->feedback_metadata();
  if (!feedback_metadata->is_empty()) {
    RecordFixedArrayHelper(sfi, feedback_metadata,
                           TYPE_FEEDBACK_METADATA_SUB_TYPE, 0);
    Object* names =
        feedback_metadata->get(TypeFeedbackMetadata::kNamesTableIndex);
    if (!names->IsSmi()) {
      UnseededNumberDictionary* names = UnseededNumberDictionary::cast(
          feedback_metadata->get(TypeFeedbackMetadata::kNamesTableIndex));
      RecordHashTableHelper(sfi, names, TYPE_FEEDBACK_METADATA_SUB_TYPE);
    }
  }

  if (!sfi->OptimizedCodeMapIsCleared()) {
    FixedArray* optimized_code_map = sfi->optimized_code_map();
    RecordFixedArrayHelper(sfi, optimized_code_map, OPTIMIZED_CODE_MAP_SUB_TYPE,
                           0);
    // Optimized code map should be small, so skip accounting.
    int len = optimized_code_map->length();
    for (int i = SharedFunctionInfo::kEntriesStart; i < len;
         i += SharedFunctionInfo::kEntryLength) {
      Object* slot =
          optimized_code_map->get(i + SharedFunctionInfo::kLiteralsOffset);
      LiteralsArray* literals = nullptr;
      if (slot->IsWeakCell()) {
        WeakCell* cell = WeakCell::cast(slot);
        if (!cell->cleared()) {
          literals = LiteralsArray::cast(cell->value());
        }
      } else {
        literals = LiteralsArray::cast(slot);
      }
      if (literals != nullptr) {
        RecordFixedArrayHelper(sfi, literals, LITERALS_ARRAY_SUB_TYPE, 0);
        RecordFixedArrayHelper(sfi, literals->feedback_vector(),
                               TYPE_FEEDBACK_VECTOR_SUB_TYPE, 0);
      }
    }
  }
}

void ObjectStatsCollector::RecordJSFunctionDetails(JSFunction* function) {
  LiteralsArray* literals = function->literals();
  RecordFixedArrayHelper(function, literals, LITERALS_ARRAY_SUB_TYPE, 0);
  RecordFixedArrayHelper(function, literals->feedback_vector(),
                         TYPE_FEEDBACK_VECTOR_SUB_TYPE, 0);
}

void ObjectStatsCollector::RecordFixedArrayDetails(FixedArray* array) {
  if (array->IsContext()) {
    RecordFixedArrayHelper(nullptr, array, CONTEXT_SUB_TYPE, 0);
  }
  if (IsCowArray(heap_, array) && CanRecordFixedArray(heap_, array)) {
    stats_->RecordFixedArraySubTypeStats(array, COPY_ON_WRITE_SUB_TYPE,
                                         array->Size(), 0);
  }
  if (array->IsNativeContext()) {
    Context* native_ctx = Context::cast(array);
    RecordHashTableHelper(array, native_ctx->template_instantiations_cache(),
                          TEMPLATE_INSTANTIATIONS_CACHE_SUB_TYPE);
  }
}

}  // namespace internal
}  // namespace v8
