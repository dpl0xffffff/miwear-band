#include "miwear/profile.h"
#include "miwear/ui/health_data.h"

guint miwear_gender_config_to_proto(guint config_gender) {
    return config_gender == MIWEAR_GENDER_FEMALE ? 2u : 1u;
}

guint miwear_gender_proto_to_config(guint proto_gender) {
    return proto_gender == 2u ? MIWEAR_GENDER_FEMALE : MIWEAR_GENDER_MALE;
}

gboolean miwear_profile_shows_cycles(guint config_gender) {
    return config_gender == MIWEAR_GENDER_FEMALE;
}

void miwear_profile_apply_defaults(AppState *state) {
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
        state->profile_goal_moving = miwear_activity_goal_default();
    }
    if (state->goal_moving == 0) {
        state->goal_moving = state->profile_goal_moving;
    }
    if (state->profile_cycle_length_days == 0) {
        state->profile_cycle_length_days = 28;
    }
}
