#include "es10c.h"
#include "es10x.private.h"

#include "derutils.h"
#include "hexutil.h"
#include "base64.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int es10c_get_profiles_info(struct euicc_ctx *ctx, struct es10c_profile_info **profiles)
{
    int fret = 0;
    struct derutils_node n_request = {
        .tag = 0xBF2D, // ProfileInfoListRequest
    };
    uint32_t reqlen;
    uint8_t *respbuf = NULL;
    unsigned resplen;

    struct derutils_node tmpnode, n_profileInfoListOk, n_ProfileInfo;

    struct es10c_profile_info *profiles_wptr;

    *profiles = NULL;

    reqlen = sizeof(ctx->apdu_request_buffer.body);
    if (derutils_pack(ctx->apdu_request_buffer.body, &reqlen, &n_request))
    {
        goto err;
    }

    if (es10x_command(ctx, &respbuf, &resplen, ctx->apdu_request_buffer.body, reqlen) < 0)
    {
        goto err;
    }

    if (derutils_unpack_find_tag(&tmpnode, n_request.tag, respbuf, resplen) < 0)
    {
        goto err;
    }

    if (derutils_unpack_find_tag(&n_profileInfoListOk, 0xA0, tmpnode.value, tmpnode.length) < 0)
    {
        goto err;
    }

    n_ProfileInfo.self.ptr = n_profileInfoListOk.value;
    n_ProfileInfo.self.length = 0;

    while (derutils_unpack_next(&n_ProfileInfo, &n_ProfileInfo, n_profileInfoListOk.value, n_profileInfoListOk.length) == 0)
    {
        struct es10c_profile_info *p;

        if (n_ProfileInfo.tag != 0xE3)
        {
            continue;
        }

        p = malloc(sizeof(struct es10c_profile_info));
        if (!p)
        {
            goto err;
        }

        memset(p, 0, sizeof(*p));

        tmpnode.self.ptr = n_ProfileInfo.value;
        tmpnode.self.length = 0;
        while (derutils_unpack_next(&tmpnode, &tmpnode, n_ProfileInfo.value, n_ProfileInfo.length) == 0)
        {
            switch (tmpnode.tag)
            {
            case 0x5A:
                euicc_hexutil_bin2gsmbcd(p->iccid, sizeof(p->iccid), tmpnode.value, tmpnode.length);
                break;
            case 0x4F:
                euicc_hexutil_bin2hex(p->isdpAid, sizeof(p->isdpAid), tmpnode.value, tmpnode.length);
                break;
            case 0x9F70:
                switch (derutils_convert_bin2long(tmpnode.value, tmpnode.length))
                {
                case ES10C_PROFILE_INFO_STATE_DISABLED:
                    p->profileState = "disabled";
                    break;
                case ES10C_PROFILE_INFO_STATE_ENABLED:
                    p->profileState = "enabled";
                    break;
                default:
                    p->profileState = "unknown";
                    break;
                }
                break;
            case 0x90:
                p->profileNickname = malloc(tmpnode.length + 1);
                if (p->profileNickname)
                {
                    memcpy(p->profileNickname, tmpnode.value, tmpnode.length);
                    p->profileNickname[tmpnode.length] = '\0';
                }
                break;
            case 0x91:
                p->serviceProviderName = malloc(tmpnode.length + 1);
                if (p->serviceProviderName)
                {
                    memcpy(p->serviceProviderName, tmpnode.value, tmpnode.length);
                    p->serviceProviderName[tmpnode.length] = '\0';
                }
                break;
            case 0x92:
                p->profileName = malloc(tmpnode.length + 1);
                if (p->profileName)
                {
                    memcpy(p->profileName, tmpnode.value, tmpnode.length);
                    p->profileName[tmpnode.length] = '\0';
                }
                break;
            case 0x93:
                switch (derutils_convert_bin2long(tmpnode.value, tmpnode.length))
                {
                case ES10C_ICON_TYPE_JPEG:
                    p->iconType = "jpeg";
                    break;
                case ES10C_ICON_TYPE_PNG:
                    p->iconType = "png";
                    break;
                default:
                    p->iconType = "unknown";
                    break;
                }
                break;
            case 0x94:
                p->icon = malloc(euicc_base64_encode_len(tmpnode.length));
                if (p->icon)
                {
                    euicc_base64_encode(p->icon, tmpnode.value, tmpnode.length);
                }
                break;
            case 0x95:
                switch (derutils_convert_bin2long(tmpnode.value, tmpnode.length))
                {
                case ES10C_PROFILE_INFO_CLASS_TEST:
                    p->profileClass = "test";
                    break;
                case ES10C_PROFILE_INFO_CLASS_PROVISIONING:
                    p->profileClass = "provisioning";
                    break;
                case ES10C_PROFILE_INFO_CLASS_OPERATIONAL:
                    p->profileClass = "operational";
                    break;
                default:
                    p->profileClass = "unknown";
                    break;
                }
                break;
            case 0xB6:
            case 0xB7:
            case 0xB8:
            case 0x99:
                fprintf(stderr, "\n[PLEASE REPORT][TODO][TAG %02X]: ", tmpnode.tag);
                for (int i = 0; i < tmpnode.self.length; i++)
                {
                    fprintf(stderr, "%02X ", tmpnode.self.ptr[i]);
                }
                fprintf(stderr, "\n");
                break;
            }
        }

        if (*profiles == NULL)
        {
            *profiles = p;
        }
        else
        {
            profiles_wptr->next = p;
        }

        profiles_wptr = p;
    }

    goto exit;

err:
    fret = -1;
    es10c_profile_info_free_all(*profiles);
exit:
    free(respbuf);
    respbuf = NULL;
    return fret;
}

static int es10c_enable_disable_profile(struct euicc_ctx *ctx, uint16_t op_tag, uint8_t id_tag, const uint8_t *id, uint8_t id_len, uint8_t refreshflag)
{
    int fret = 0;
    struct derutils_node n_request = {
        .tag = op_tag,
        .pack = {
            .child = &(struct derutils_node){
                .tag = 0xA0, // profileIdentifier
                .pack = {
                    .child = &(struct derutils_node){
                        .tag = id_tag,
                        .value = id,
                        .length = id_len,
                    },
                    .next = &(struct derutils_node){
                        .tag = 0x81, // refreshFlag
                        .value = &refreshflag,
                        .length = 1,
                    },
                },
            },
        },
    };
    uint32_t reqlen;
    uint8_t *respbuf = NULL;
    unsigned resplen;

    struct derutils_node tmpnode;

    if (refreshflag)
    {
        refreshflag = 0xFF;
    }

    reqlen = sizeof(ctx->apdu_request_buffer.body);
    if (derutils_pack(ctx->apdu_request_buffer.body, &reqlen, &n_request))
    {
        goto err;
    }

    if (es10x_command(ctx, &respbuf, &resplen, ctx->apdu_request_buffer.body, reqlen) < 0)
    {
        goto err;
    }

    if (derutils_unpack_find_tag(&tmpnode, n_request.tag, respbuf, resplen) < 0)
    {
        goto err;
    }

    if (derutils_unpack_find_tag(&tmpnode, 0x80, tmpnode.value, tmpnode.length) < 0)
    {
        goto err;
    }

    fret = derutils_convert_bin2long(tmpnode.value, tmpnode.length);

    goto exit;

err:
    fret = -1;
exit:
    free(respbuf);
    respbuf = NULL;
    return fret;
}

static int es10c_enable_disable_profile_aid(struct euicc_ctx *ctx, uint16_t op_tag, const char *aid, char refreshflag)
{
    uint8_t asn1aid[16];

    if (euicc_hexutil_hex2bin(asn1aid, sizeof(asn1aid), aid) < 0)
    {
        return -1;
    }

    return es10c_enable_disable_profile(ctx, op_tag, 0x4F, asn1aid, sizeof(asn1aid), refreshflag);
}

static int es10c_enable_disable_profile_iccid(struct euicc_ctx *ctx, uint16_t op_tag, const char *iccid, char refreshflag)
{
    uint8_t asn1iccid[10];

    if (euicc_hexutil_gsmbcd2bin(asn1iccid, sizeof(asn1iccid), iccid) < 0)
    {
        return -1;
    }

    return es10c_enable_disable_profile(ctx, op_tag, 0x5A, asn1iccid, sizeof(asn1iccid), refreshflag);
}

int es10c_enable_profile_aid(struct euicc_ctx *ctx, const char *aid, char refreshflag)
{
    return es10c_enable_disable_profile_aid(ctx, 0xBF31, aid, refreshflag);
}

int es10c_enable_profile_iccid(struct euicc_ctx *ctx, const char *iccid, char refreshflag)
{
    return es10c_enable_disable_profile_iccid(ctx, 0xBF31, iccid, refreshflag);
}

int es10c_disable_profile_aid(struct euicc_ctx *ctx, const char *aid, char refreshflag)
{
    return es10c_enable_disable_profile_aid(ctx, 0xBF32, aid, refreshflag);
}

int es10c_disable_profile_iccid(struct euicc_ctx *ctx, const char *iccid, char refreshflag)
{
    return es10c_enable_disable_profile_iccid(ctx, 0xBF32, iccid, refreshflag);
}

static int es10c_delete_profile(struct euicc_ctx *ctx, uint8_t id_tag, const uint8_t *id, uint8_t id_len)
{
    int fret = 0;
    struct derutils_node n_request = {
        .tag = 0xBF33, // DeleteProfileRequest
        .pack = {
            .child = &(struct derutils_node){
                .tag = id_tag,
                .value = id,
                .length = id_len,
            },
        },
    };
    uint32_t reqlen;
    uint8_t *respbuf = NULL;
    unsigned resplen;

    struct derutils_node tmpnode;

    reqlen = sizeof(ctx->apdu_request_buffer.body);
    if (derutils_pack(ctx->apdu_request_buffer.body, &reqlen, &n_request))
    {
        goto err;
    }

    if (es10x_command(ctx, &respbuf, &resplen, ctx->apdu_request_buffer.body, reqlen) < 0)
    {
        goto err;
    }

    if (derutils_unpack_find_tag(&tmpnode, n_request.tag, respbuf, resplen) < 0)
    {
        goto err;
    }

    if (derutils_unpack_find_tag(&tmpnode, 0x80, tmpnode.value, tmpnode.length) < 0)
    {
        goto err;
    }

    fret = derutils_convert_bin2long(tmpnode.value, tmpnode.length);

    goto exit;

err:
    fret = -1;
exit:
    free(respbuf);
    respbuf = NULL;
    return fret;
}

int es10c_delete_profile_aid(struct euicc_ctx *ctx, const char *aid)
{
    uint8_t asn1aid[16];

    if (euicc_hexutil_hex2bin(asn1aid, sizeof(asn1aid), aid) < 0)
    {
        return -1;
    }

    return es10c_delete_profile(ctx, 0x4F, asn1aid, sizeof(asn1aid));
}

int es10c_delete_profile_iccid(struct euicc_ctx *ctx, const char *iccid)
{
    uint8_t asn1iccid[10];

    if (euicc_hexutil_gsmbcd2bin(asn1iccid, sizeof(asn1iccid), iccid) < 0)
    {
        return -1;
    }

    return es10c_delete_profile(ctx, 0x5A, asn1iccid, sizeof(asn1iccid));
}

int es10c_euicc_memory_reset(struct euicc_ctx *ctx)
{
    int fret = 0;
    uint8_t resetOptions[2];
    struct derutils_node n_request = {
        .tag = 0xBF34, // EuiccMemoryResetRequest
        .pack = {
            .child = &(struct derutils_node){
                .tag = 0x82, // resetOptions
                .value = resetOptions,
                .length = sizeof(resetOptions),
            },
        },
    };
    uint32_t reqlen;
    uint8_t *respbuf = NULL;
    unsigned resplen;

    struct derutils_node tmpnode;

    if (derutils_convert_bits2bin(resetOptions, sizeof(resetOptions), (const uint32_t[]){0, 1, 2}, 3) < 0)
    {
        goto err;
    }

    reqlen = sizeof(ctx->apdu_request_buffer.body);
    if (derutils_pack(ctx->apdu_request_buffer.body, &reqlen, &n_request))
    {
        goto err;
    }

    if (es10x_command(ctx, &respbuf, &resplen, ctx->apdu_request_buffer.body, reqlen) < 0)
    {
        goto err;
    }

    if (derutils_unpack_find_tag(&tmpnode, n_request.tag, respbuf, resplen) < 0)
    {
        goto err;
    }

    if (derutils_unpack_find_tag(&tmpnode, 0x80, tmpnode.value, tmpnode.length) < 0)
    {
        goto err;
    }

    fret = derutils_convert_bin2long(tmpnode.value, tmpnode.length);

    goto exit;

err:
    fret = -1;
exit:
    free(respbuf);
    respbuf = NULL;
    return fret;
}

int es10c_get_eid(struct euicc_ctx *ctx, char **eid)
{
    int fret = 0;
    struct derutils_node n_request = {
        .tag = 0xBF3E, // GetEuiccDataRequest
        .pack = {
            .child = &(struct derutils_node){
                .tag = 0x5C, // tagList
                .length = 1,
                .value = (uint8_t[]){0x5A},
            },
        },
    };
    uint32_t reqlen;
    uint8_t *respbuf = NULL;
    unsigned resplen;

    struct derutils_node tmpnode;

    reqlen = sizeof(ctx->apdu_request_buffer.body);
    if (derutils_pack(ctx->apdu_request_buffer.body, &reqlen, &n_request))
    {
        goto err;
    }

    if (es10x_command(ctx, &respbuf, &resplen, ctx->apdu_request_buffer.body, reqlen) < 0)
    {
        goto err;
    }

    if (derutils_unpack_find_tag(&tmpnode, n_request.tag, respbuf, resplen))
    {
        goto err;
    }

    if (derutils_unpack_find_tag(&tmpnode, 0x5A, tmpnode.value, tmpnode.length))
    {
        goto err;
    }

    *eid = malloc((tmpnode.length * 2) + 1);
    if (*eid == NULL)
    {
        goto err;
    }

    euicc_hexutil_bin2hex(*eid, (tmpnode.length * 2) + 1, tmpnode.value, tmpnode.length);

    goto exit;

err:
    fret = -1;
    free(*eid);
    *eid = NULL;
exit:
    free(respbuf);
    respbuf = NULL;
    return fret;
}

int es10c_set_nickname(struct euicc_ctx *ctx, const char *iccid, const char *nickname)
{
    int fret = 0;
    uint8_t asn1iccid[10];
    struct derutils_node n_request = {
        .tag = 0xBF29, // SetNicknameRequest
        .pack = {
            .child = &(struct derutils_node){
                .tag = 0x5A, // iccid
                .value = asn1iccid,
                .length = sizeof(asn1iccid),
                .pack = {
                    .next = &(struct derutils_node){
                        .tag = 0x90, // profileNickname
                        .length = strlen(nickname),
                        .value = nickname,
                    },
                },
            },
        },
    };
    uint32_t reqlen;
    uint8_t *respbuf = NULL;
    unsigned resplen;

    struct derutils_node tmpnode;

    if (euicc_hexutil_gsmbcd2bin(asn1iccid, sizeof(asn1iccid), iccid) < 0)
    {
        goto err;
    }

    reqlen = sizeof(ctx->apdu_request_buffer.body);
    if (derutils_pack(ctx->apdu_request_buffer.body, &reqlen, &n_request))
    {
        goto err;
    }

    if (es10x_command(ctx, &respbuf, &resplen, ctx->apdu_request_buffer.body, reqlen) < 0)
    {
        goto err;
    }

    if (derutils_unpack_find_tag(&tmpnode, n_request.tag, respbuf, resplen) < 0)
    {
        goto err;
    }

    if (derutils_unpack_find_tag(&tmpnode, 0x80, tmpnode.value, tmpnode.length) < 0)
    {
        goto err;
    }

    fret = derutils_convert_bin2long(tmpnode.value, tmpnode.length);

    goto exit;

err:
    fret = -1;
exit:
    free(respbuf);
    respbuf = NULL;
    return fret;
}

void es10c_profile_info_free_all(struct es10c_profile_info *profiles)
{
    while (profiles)
    {
        struct es10c_profile_info *next = profiles->next;
        free(profiles->profileNickname);
        free(profiles->serviceProviderName);
        free(profiles->profileName);
        free(profiles->icon);
        free(profiles);
        profiles = next;
    }
}
