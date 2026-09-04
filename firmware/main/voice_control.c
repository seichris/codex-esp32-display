#include "voice_control.h"

#include <string.h>

void voice_control_init(voice_control_t *control)
{
    if (control == NULL) return;
    memset(control, 0, sizeof(*control));
    control->state = ATTENTION_VOICE_READY;
}

voice_control_action_t voice_control_begin_toggle(voice_control_t *control, const char *thread_id)
{
    if (control == NULL || thread_id == NULL || thread_id[0] == '\0') {
        return VOICE_CONTROL_ACTION_NONE;
    }
    const bool same_thread = strcmp(control->thread_id, thread_id) == 0;
    if (same_thread && (control->state == ATTENTION_VOICE_FOCUSING
        || control->state == ATTENTION_VOICE_STARTING
        || control->state == ATTENTION_VOICE_LISTENING)) {
        control->state = ATTENTION_VOICE_MUTED;
        return VOICE_CONTROL_ACTION_MUTE;
    }

    strlcpy(control->thread_id, thread_id, sizeof(control->thread_id));
    control->state = ATTENTION_VOICE_FOCUSING;
    return VOICE_CONTROL_ACTION_FOCUS;
}

voice_control_action_t voice_control_focus_result(
    voice_control_t *control,
    bool success,
    const char *acknowledged_thread_id
)
{
    if (control == NULL) return VOICE_CONTROL_ACTION_NONE;
    if (control->state != ATTENTION_VOICE_FOCUSING) {
        return VOICE_CONTROL_ACTION_NONE;
    }
    if (!success || acknowledged_thread_id == NULL
        || strcmp(control->thread_id, acknowledged_thread_id) != 0) {
        control->state = ATTENTION_VOICE_ERROR;
        return VOICE_CONTROL_ACTION_NONE;
    }
    control->state = ATTENTION_VOICE_STARTING;
    return VOICE_CONTROL_ACTION_START;
}

void voice_control_voice_result(voice_control_t *control, bool success, bool listening)
{
    if (control == NULL) return;
    control->state = success
        ? (listening ? ATTENTION_VOICE_LISTENING : ATTENTION_VOICE_MUTED)
        : ATTENTION_VOICE_ERROR;
}

void voice_control_reconcile(
    voice_control_t *control,
    const char *thread_id,
    attention_voice_state_t state
)
{
    if (control == NULL) return;
    if (thread_id != NULL && thread_id[0] != '\0') {
        strlcpy(control->thread_id, thread_id, sizeof(control->thread_id));
    }
    control->state = state;
}
