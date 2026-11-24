#ifndef MIWEAR_PROFILE_H
#define MIWEAR_PROFILE_H

#include "miwear/app.h"

/* Config.ini [profile] gender: 0 = female (cycles), 1 = male. Band UserInfo uses 1/2. */
#define MIWEAR_GENDER_FEMALE 0
#define MIWEAR_GENDER_MALE 1

void miwear_profile_apply_defaults(AppState *state);

guint miwear_gender_config_to_proto(guint config_gender);
gboolean miwear_profile_shows_cycles(guint config_gender);

#endif
