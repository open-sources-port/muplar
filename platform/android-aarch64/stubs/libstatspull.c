#include <stddef.h>
#include <stdint.h>

void AStatsManager_setPullAtomCallback(int32_t atomId, void* metadata, void* callback, void* cookie) {
    (void)atomId; (void)metadata; (void)callback; (void)cookie;
}
void AStatsManager_clearPullAtomCallback(int32_t atomId) {
    (void)atomId;
}
void AStatsEventList_addStatsEvent(void* list, void* event) {
    (void)list; (void)event;
}
