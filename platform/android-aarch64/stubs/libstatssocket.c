#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct AStatsEvent AStatsEvent;

AStatsEvent *AStatsEvent_obtain(void)
{
    return NULL;
}
void AStatsEvent_setAtomId(AStatsEvent *event, uint32_t atomId)
{
    (void) event;
    (void) atomId;
}
void AStatsEvent_writeBool(AStatsEvent *event, bool value)
{
    (void) event;
    (void) value;
}
void AStatsEvent_writeInt32(AStatsEvent *event, int32_t value)
{
    (void) event;
    (void) value;
}
void AStatsEvent_writeInt64(AStatsEvent *event, int64_t value)
{
    (void) event;
    (void) value;
}
void AStatsEvent_writeFloat(AStatsEvent *event, float value)
{
    (void) event;
    (void) value;
}
void AStatsEvent_writeString(AStatsEvent *event, const char *value)
{
    (void) event;
    (void) value;
}
void AStatsEvent_writeByteArray(AStatsEvent *event,
                                const uint8_t *buf,
                                size_t numBytes)
{
    (void) event;
    (void) buf;
    (void) numBytes;
}
void AStatsEvent_write(AStatsEvent *event)
{
    (void) event;
}
void AStatsEvent_release(AStatsEvent *event)
{
    (void) event;
}
void AStatsEvent_build(AStatsEvent *event)
{
    (void) event;
}
void AStatsEvent_addBoolAnnotation(AStatsEvent *event,
                                   uint8_t annotationId,
                                   bool value)
{
    (void) event;
    (void) annotationId;
    (void) value;
}
void AStatsEvent_addInt32Annotation(AStatsEvent *event,
                                    uint8_t annotationId,
                                    int32_t value)
{
    (void) event;
    (void) annotationId;
    (void) value;
}
