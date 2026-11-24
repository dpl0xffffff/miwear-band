#include "salus/profile.h"
#include "salus/store/health_data.h"

guint salus_gender_config_to_proto(guint config_gender) {
    return config_gender == SALUS_GENDER_FEMALE ? 2u : 1u;
}

gboolean salus_profile_shows_cycles(guint config_gender) {
    return config_gender == SALUS_GENDER_FEMALE;
}

void salus_profile_apply_defaults(AppState *state) {
    if (state == NULL) {
        return;
    }
    if (state->profile_height_cm == 0) {
        state->profile_height_cm = 170;
    }
    if (state->profile_weight_kg <= 0.0f) {
        state->profile_weight_kg = 70.0f;
    }
    if (state->profile_birthday_yyyymmdd == 0) {
        state->profile_birthday_yyyymmdd = 19900101u;
    }
    if (state->profile_max_hr == 0) {
        state->profile_max_hr = 185;
    }
    if (state->profile_goal_moving == 0) {
        state->profile_goal_moving = salus_activity_goal_default();
    }
    if (state->goal_moving == 0) {
        state->goal_moving = state->profile_goal_moving;
    }
    if (state->profile_cycle_length_days == 0) {
        state->profile_cycle_length_days = 28;
    }
}
