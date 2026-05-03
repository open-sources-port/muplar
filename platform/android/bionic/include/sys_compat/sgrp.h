#ifndef SGRP_H
#define SGRP_H

typedef struct sgrp {
    char *sg_namp;     // group name
    char *sg_passwd;   // group password
    char **sg_mem;     // group members
    char **sg_adm;     // group administrators
} sgrp;

#endif
