#ifndef SALUS_PROFILE_H
#define SALUS_PROFILE_H

#include "salus/app.h"

/* Config.ini [profile] gender: 0 = female (cycles), 1 = male. Band UserInfo uses 1/2. */
#define SALUS_GENDER_FEMALE 0
#define SALUS_GENDER_MALE 1

void salus_profile_apply_defaults(SalusApp *state);

guint salus_gender_config_to_proto(guint config_gender);
gboolean salus_profile_shows_cycles(guint config_gender);

#endif
