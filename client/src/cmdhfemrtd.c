//-----------------------------------------------------------------------------
// Copyright (C) 2020 A. Ozkal
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// High frequency Electronic Machine Readable Travel Document commands
//-----------------------------------------------------------------------------

// This code is heavily based on mrpkey.py of RFIDIOt

#include "cmdhfemrtd.h"
#include <ctype.h>
#include "fileutils.h"              // saveFile
#include "cmdparser.h"              // command_t
#include "cmdtrace.h"               // CmdTraceList
#include "cliparser.h"              // CLIParserContext etc
#include "cmdhf14a.h"               // ExchangeAPDU14a
#include "protocols.h"              // definitions of ISO14A/7816 protocol
#include "emv/apduinfo.h"           // GetAPDUCodeDescription
#include "crypto/libpcrypto.h"      // Hash calculation (sha1, sha256, sha512)
#include "mifare/desfire_crypto.h"  // des_encrypt/des_decrypt
#include "des.h"                    // mbedtls_des_key_set_parity
#include "cmdhf14b.h"               // exchange_14b_apdu
#include "iso14b.h"                 // ISO14B_CONNECT etc
#include "crapto1/crapto1.h"        // prng_successor
#include "commonutil.h"             // num_to_bytes
#include "util_posix.h"             // msclock

// Max file size in bytes. Used in several places.
// Average EF_DG2 seems to be around 20-25kB or so, but ICAO doesn't set an upper limit
// Iris data seems to be suggested to be around 35kB per eye (Presumably bumping up the file size to around 70kB)
// but as we cannot read that until we implement PACE, 35k seems to be a safe point.
#define EMRTD_MAX_FILE_SIZE 35000

// ISO7816 commands
#define EMRTD_SELECT "A4"
#define EMRTD_EXTERNAL_AUTHENTICATE "82"
#define EMRTD_GET_CHALLENGE "84"
#define EMRTD_READ_BINARY "B0"
#define EMRTD_P1_SELECT_BY_EF "02"
#define EMRTD_P1_SELECT_BY_NAME "04"
#define EMRTD_P2_PROPRIETARY "0C"

// App IDs
#define EMRTD_AID_MRTD "A0000002471001"

// DESKey Types
const uint8_t KENC_type[4] = {0x00, 0x00, 0x00, 0x01};
const uint8_t KMAC_type[4] = {0x00, 0x00, 0x00, 0x02};

static int emrtd_dump_ef_dg2(uint8_t *file_contents, size_t file_length);
static int emrtd_dump_ef_dg5(uint8_t *file_contents, size_t file_length);
static int emrtd_dump_ef_dg7(uint8_t *file_contents, size_t file_length);
static int emrtd_dump_ef_sod(uint8_t *file_contents, size_t file_length);
static int emrtd_print_ef_com_info(uint8_t *data, size_t datalen);
static int emrtd_print_ef_dg1_info(uint8_t *data, size_t datalen);
static int emrtd_print_ef_dg11_info(uint8_t *data, size_t datalen);
static int emrtd_print_ef_dg12_info(uint8_t *data, size_t datalen);

typedef enum  { // list must match dg_table
    EF_COM = 0,
    EF_DG1,
    EF_DG2,
    EF_DG3,
    EF_DG4,
    EF_DG5,
    EF_DG6,
    EF_DG7,
    EF_DG8,
    EF_DG9,
    EF_DG10,
    EF_DG11,
    EF_DG12,
    EF_DG13,
    EF_DG14,
    EF_DG15,
    EF_DG16,
    EF_SOD,
    EF_CardAccess,
    EF_CardSecurity,
} emrtd_dg_enum;

static emrtd_dg_t dg_table[] = {
//  tag    dg# fileid  filename           desc                                                  pace   eac    req    fast   parser                    dumper
    {0x60, 0,  "011E", "EF_COM",          "Header and Data Group Presence Information",         false, false, true,  true,  emrtd_print_ef_com_info,  NULL},
    {0x61, 1,  "0101", "EF_DG1",          "Details recorded in MRZ",                            false, false, true,  true,  emrtd_print_ef_dg1_info,  NULL},
    {0x75, 2,  "0102", "EF_DG2",          "Encoded Face",                                       false, false, true,  false, NULL,                     emrtd_dump_ef_dg2},
    {0x63, 3,  "0103", "EF_DG3",          "Encoded Finger(s)",                                  false, true,  false, false, NULL,                     NULL},
    {0x76, 4,  "0104", "EF_DG4",          "Encoded Eye(s)",                                     false, true,  false, false, NULL,                     NULL},
    {0x65, 5,  "0105", "EF_DG5",          "Displayed Portrait",                                 false, false, false, false, NULL,                     emrtd_dump_ef_dg5},
    {0x66, 6,  "0106", "EF_DG6",          "Reserved for Future Use",                            false, false, false, false, NULL,                     NULL},
    {0x67, 7,  "0107", "EF_DG7",          "Displayed Signature or Usual Mark",                  false, false, false, false, NULL,                     emrtd_dump_ef_dg7},
    {0x68, 8,  "0108", "EF_DG8",          "Data Feature(s)",                                    false, false, false, true,  NULL,                     NULL},
    {0x69, 9,  "0109", "EF_DG9",          "Structure Feature(s)",                               false, false, false, true,  NULL,                     NULL},
    {0x6a, 10, "010A", "EF_DG10",         "Substance Feature(s)",                               false, false, false, true,  NULL,                     NULL},
    {0x6b, 11, "010B", "EF_DG11",         "Additional Personal Detail(s)",                      false, false, false, true,  emrtd_print_ef_dg11_info, NULL},
    {0x6c, 12, "010C", "EF_DG12",         "Additional Document Detail(s)",                      false, false, false, true,  emrtd_print_ef_dg12_info, NULL},
    {0x6d, 13, "010D", "EF_DG13",         "Optional Detail(s)",                                 false, false, false, true,  NULL,                     NULL},
    {0x6e, 14, "010E", "EF_DG14",         "Security Options",                                   false, false, false, true,  NULL,                     NULL},
    {0x6f, 15, "010F", "EF_DG15",         "Active Authentication Public Key Info",              false, false, false, true,  NULL,                     NULL},
    {0x70, 16, "0110", "EF_DG16",         "Person(s) to Notify",                                false, false, false, true,  NULL,                     NULL},
    {0x77, 0,  "011D", "EF_SOD",          "Document Security Object",                           false, false, false, false, NULL,                     emrtd_dump_ef_sod},
    {0xff, 0,  "011C", "EF_CardAccess",   "PACE SecurityInfos",                                 true,  false, true,  true,  NULL,                     NULL},
    {0xff, 0,  "011D", "EF_CardSecurity", "PACE SecurityInfos for Chip Authentication Mapping", true,  false, false, true,  NULL,                     NULL},
    {0x00, 0,  NULL, NULL, NULL, false, false, false, false, NULL, NULL}
};

// https://security.stackexchange.com/questions/131241/where-do-magic-constants-for-signature-algorithms-come-from
// https://tools.ietf.org/html/rfc3447#page-43
static emrtd_hashalg_t hashalg_table[] = {
//  name        hash func   len len descriptor
    {"SHA-1",   sha1hash,   20,  7, {0x06, 0x05, 0x2B, 0x0E, 0x03, 0x02, 0x1A}},
    {"SHA-256", sha256hash, 32, 11, {0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01}},
    {"SHA-512", sha512hash, 64, 11, {0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03}},
    {NULL,      NULL,       0,  0,  {}}
};

static emrtd_dg_t *emrtd_tag_to_dg(uint8_t tag) {
    for (int dgi = 0; dg_table[dgi].filename != NULL; dgi++) {
        if (dg_table[dgi].tag == tag) {
            return &dg_table[dgi];
        }
    }
    return NULL;
}
static emrtd_dg_t *emrtd_fileid_to_dg(const char *file_id) {
    for (int dgi = 0; dg_table[dgi].filename != NULL; dgi++) {
        if (strcmp(dg_table[dgi].fileid, file_id) == 0) {
            return &dg_table[dgi];
        }
    }
    return NULL;
}

static int CmdHelp(const char *Cmd);

static uint16_t get_sw(uint8_t *d, uint8_t n) {
    if (n < 2)
        return 0;

    n -= 2;
    return d[n] * 0x0100 + d[n + 1];
}

static bool emrtd_exchange_commands(const char *cmd, uint8_t *dataout, int *dataoutlen, bool activate_field, bool keep_field_on, bool use_14b) {
    uint8_t response[PM3_CMD_DATA_SIZE];
    int resplen = 0;

    PrintAndLogEx(DEBUG, "Sending: %s", cmd);

    uint8_t aCMD[PM3_CMD_DATA_SIZE];
    int aCMD_n = 0;
    param_gethex_to_eol(cmd, 0, aCMD, sizeof(aCMD), &aCMD_n);
    int res;
    if (use_14b) {
        // need to add a long timeout for passports with activated anti-bruteforce measure
        res = exchange_14b_apdu(aCMD, aCMD_n, activate_field, keep_field_on, response, sizeof(response), &resplen, 15000);
    } else {
        res = ExchangeAPDU14a(aCMD, aCMD_n, activate_field, keep_field_on, response, sizeof(response), &resplen);
    }
    if (res) {
        DropField();
        return false;
    }

    if (resplen < 2) {
        return false;
    }
    PrintAndLogEx(DEBUG, "Response: %s", sprint_hex(response, resplen));

    // drop sw
    memcpy(dataout, &response, resplen - 2);
    *dataoutlen = (resplen - 2);

    uint16_t sw = get_sw(response, resplen);
    if (sw != 0x9000) {
        PrintAndLogEx(DEBUG, "Command %s failed (%04x - %s).", cmd, sw, GetAPDUCodeDescription(sw >> 8, sw & 0xff));
        return false;
    }
    return true;
}

static int emrtd_exchange_commands_noout(const char *cmd, bool activate_field, bool keep_field_on, bool use_14b) {
    uint8_t response[PM3_CMD_DATA_SIZE];
    int resplen = 0;

    return emrtd_exchange_commands(cmd, response, &resplen, activate_field, keep_field_on, use_14b);
}

static char emrtd_calculate_check_digit(char *data) {
    int mrz_weight[] = {7, 3, 1};
    int cd = 0;
    int value = 0;
    char d;

    for (int i = 0; i < strlen(data); i++) {
        d = data[i];
        if ('A' <= d && d <= 'Z') {
            value = d - 55;
        } else if ('a' <= d && d <= 'z') {
            value = d - 87;
        } else if (d == '<') {
            value = 0;
        } else {  // Numbers
            value = d - 48;
        }
        cd += value * mrz_weight[i % 3];
    }
    return cd % 10;
}

static int emrtd_get_asn1_data_length(uint8_t *datain, int datainlen, int offset) {
    PrintAndLogEx(DEBUG, "asn1 datalength, datain: %s", sprint_hex_inrow(datain, datainlen));
    int lenfield = (int) * (datain + offset);
    PrintAndLogEx(DEBUG, "asn1 datalength, lenfield: %02X", lenfield);
    if (lenfield <= 0x7f) {
        return lenfield;
    } else if (lenfield == 0x80) {
        // TODO: 0x80 means indeterminate, and this impl is a workaround.
        // Giving rest of the file is a workaround, nothing more, nothing less.
        // https://wf.lavatech.top/ave-but-random/emrtd-data-quirks#EF_SOD
        return datainlen;
    } else if (lenfield == 0x81) {
        return ((int) * (datain + offset + 1));
    } else if (lenfield == 0x82) {
        return ((int) * (datain + offset + 1) << 8) | ((int) * (datain + offset + 2));
    } else if (lenfield == 0x83) {
        return (((int) * (datain + offset + 1) << 16) | ((int) * (datain + offset + 2)) << 8) | ((int) * (datain + offset + 3));
    }
    return false;
}

static int emrtd_get_asn1_field_length(uint8_t *datain, int datainlen, int offset) {
    PrintAndLogEx(DEBUG, "asn1 fieldlength, datain: %s", sprint_hex_inrow(datain, datainlen));
    int lenfield = (int) * (datain + offset);
    PrintAndLogEx(DEBUG, "asn1 fieldlength, lenfield: %02X", lenfield);
    if (lenfield <= 0x80) {
        return 1;
    } else if (lenfield == 0x81) {
        return 2;
    } else if (lenfield == 0x82) {
        return 3;
    } else if (lenfield == 0x83) {
        return 4;
    }
    return false;
}

static void des_encrypt_ecb(uint8_t *key, uint8_t *input, uint8_t *output) {
    mbedtls_des_context ctx_enc;
    mbedtls_des_setkey_enc(&ctx_enc, key);
    mbedtls_des_crypt_ecb(&ctx_enc, input, output);
    mbedtls_des_free(&ctx_enc);
}

static void des_decrypt_ecb(uint8_t *key, uint8_t *input, uint8_t *output) {
    mbedtls_des_context ctx_dec;
    mbedtls_des_setkey_dec(&ctx_dec, key);
    mbedtls_des_crypt_ecb(&ctx_dec, input, output);
    mbedtls_des_free(&ctx_dec);
}

static void des3_encrypt_cbc(uint8_t *iv, uint8_t *key, uint8_t *input, int inputlen, uint8_t *output) {
    mbedtls_des3_context ctx;
    mbedtls_des3_set2key_enc(&ctx, key);

    mbedtls_des3_crypt_cbc(&ctx  // des3_context
                           , MBEDTLS_DES_ENCRYPT    // int mode
                           , inputlen               // length
                           , iv                     // iv[8]
                           , input                  // input
                           , output                 // output
                          );
    mbedtls_des3_free(&ctx);
}

static void des3_decrypt_cbc(uint8_t *iv, uint8_t *key, uint8_t *input, int inputlen, uint8_t *output) {
    mbedtls_des3_context ctx;
    mbedtls_des3_set2key_dec(&ctx, key);

    mbedtls_des3_crypt_cbc(&ctx  // des3_context
                           , MBEDTLS_DES_DECRYPT    // int mode
                           , inputlen               // length
                           , iv                     // iv[8]
                           , input                  // input
                           , output                 // output
                          );
    mbedtls_des3_free(&ctx);
}

static int pad_block(uint8_t *input, int inputlen, uint8_t *output) {
    uint8_t padding[8] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    memcpy(output, input, inputlen);

    int to_pad = (8 - (inputlen % 8));

    for (int i = 0; i < to_pad; i++) {
        output[inputlen + i] = padding[i];
    }

    return inputlen + to_pad;
}

static void retail_mac(uint8_t *key, uint8_t *input, int inputlen, uint8_t *output) {
    // This code assumes blocklength (n) = 8, and input len of up to 240 or so chars
    // This code takes inspirations from https://github.com/devinvenable/iso9797algorithm3
    uint8_t k0[8];
    uint8_t k1[8];
    uint8_t intermediate[8] = {0x00};
    uint8_t intermediate_des[256];
    uint8_t block[8];
    uint8_t message[256];

    // Populate keys
    memcpy(k0, key, 8);
    memcpy(k1, key + 8, 8);

    // Prepare message
    int blocksize = pad_block(input, inputlen, message);

    // Do chaining and encryption
    for (int i = 0; i < (blocksize / 8); i++) {
        memcpy(block, message + (i * 8), 8);

        // XOR
        for (int x = 0; x < 8; x++) {
            intermediate[x] = intermediate[x] ^ block[x];
        }

        des_encrypt_ecb(k0, intermediate, intermediate_des);
        memcpy(intermediate, intermediate_des, 8);
    }


    des_decrypt_ecb(k1, intermediate, intermediate_des);
    memcpy(intermediate, intermediate_des, 8);

    des_encrypt_ecb(k0, intermediate, intermediate_des);
    memcpy(output, intermediate_des, 8);
}

static void emrtd_deskey(uint8_t *seed, const uint8_t *type, int length, uint8_t *dataout) {
    PrintAndLogEx(DEBUG, "seed.............. %s", sprint_hex_inrow(seed, 16));

    // combine seed and type
    uint8_t data[50];
    memcpy(data, seed, length);
    memcpy(data + length, type, 4);
    PrintAndLogEx(DEBUG, "data.............. %s", sprint_hex_inrow(data, length + 4));

    // SHA1 the key
    unsigned char key[64];
    sha1hash(data, length + 4, key);
    PrintAndLogEx(DEBUG, "key............... %s", sprint_hex_inrow(key, length + 4));

    // Set parity bits
    for (int i = 0; i < ((length + 4) / 8); i++) {
        mbedtls_des_key_set_parity(key + (i * 8));
    }
    PrintAndLogEx(DEBUG, "post-parity key... %s", sprint_hex_inrow(key, 20));

    memcpy(dataout, &key, length);
}

static int emrtd_select_file(const char *select_by, const char *file_id, bool use_14b) {
    int file_id_len = strlen(file_id) / 2;

    char cmd[50];
    sprintf(cmd, "00%s%s0C%02X%s", EMRTD_SELECT, select_by, file_id_len, file_id);

    return emrtd_exchange_commands_noout(cmd, false, true, use_14b);
}

static int emrtd_get_challenge(int length, uint8_t *dataout, int *dataoutlen, bool use_14b) {
    char cmd[50];
    sprintf(cmd, "00%s0000%02X", EMRTD_GET_CHALLENGE, length);

    return emrtd_exchange_commands(cmd, dataout, dataoutlen, false, true, use_14b);
}

static int emrtd_external_authenticate(uint8_t *data, int length, uint8_t *dataout, int *dataoutlen, bool use_14b) {
    char cmd[100];
    sprintf(cmd, "00%s0000%02X%s%02X", EMRTD_EXTERNAL_AUTHENTICATE, length, sprint_hex_inrow(data, length), length);
    return emrtd_exchange_commands(cmd, dataout, dataoutlen, false, true, use_14b);
}

static int _emrtd_read_binary(int offset, int bytes_to_read, uint8_t *dataout, int *dataoutlen, bool use_14b) {
    char cmd[50];
    sprintf(cmd, "00%s%04X%02X", EMRTD_READ_BINARY, offset, bytes_to_read);

    return emrtd_exchange_commands(cmd, dataout, dataoutlen, false, true, use_14b);
}

static void emrtd_bump_ssc(uint8_t *ssc) {
    PrintAndLogEx(DEBUG, "ssc-b: %s", sprint_hex_inrow(ssc, 8));
    for (int i = 7; i > 0; i--) {
        if ((*(ssc + i)) == 0xFF) {
            // Set anything already FF to 0, we'll do + 1 on num to left anyways
            (*(ssc + i)) = 0;
        } else {
            (*(ssc + i)) += 1;
            PrintAndLogEx(DEBUG, "ssc-a: %s", sprint_hex_inrow(ssc, 8));
            return;
        }
    }
}

static bool emrtd_check_cc(uint8_t *ssc, uint8_t *key, uint8_t *rapdu, int rapdulength) {
    // https://elixi.re/i/clarkson.png
    uint8_t k[500];
    uint8_t cc[500];

    emrtd_bump_ssc(ssc);

    memcpy(k, ssc, 8);
    int length = 0;
    int length2 = 0;

    if (*(rapdu) == 0x87) {
        length += 2 + (*(rapdu + 1));
        memcpy(k + 8, rapdu, length);
        PrintAndLogEx(DEBUG, "len1: %i", length);
    }

    if ((*(rapdu + length)) == 0x99) {
        length2 += 2 + (*(rapdu + (length + 1)));
        memcpy(k + length + 8, rapdu + length, length2);
        PrintAndLogEx(DEBUG, "len2: %i", length2);
    }

    int klength = length + length2 + 8;

    retail_mac(key, k, klength, cc);
    PrintAndLogEx(DEBUG, "cc: %s", sprint_hex_inrow(cc, 8));
    PrintAndLogEx(DEBUG, "rapdu: %s", sprint_hex_inrow(rapdu, rapdulength));
    PrintAndLogEx(DEBUG, "rapdu cut: %s", sprint_hex_inrow(rapdu + (rapdulength - 8), 8));
    PrintAndLogEx(DEBUG, "k: %s", sprint_hex_inrow(k, klength));

    return memcmp(cc, rapdu + (rapdulength - 8), 8) == 0;
}

static void _emrtd_convert_filename(const char *file, uint8_t *dataout) {
    char temp[3] = {0x00};
    memcpy(temp, file, 2);
    dataout[0] = (int)strtol(temp, NULL, 16);
    memcpy(temp, file + 2, 2);
    dataout[1] = (int)strtol(temp, NULL, 16);
}

static bool emrtd_secure_select_file(uint8_t *kenc, uint8_t *kmac, uint8_t *ssc, const char *select_by, const char *file, bool use_14b) {
    uint8_t response[PM3_CMD_DATA_SIZE];
    int resplen = 0;

    // convert filename of string to bytes
    uint8_t file_id[2];
    _emrtd_convert_filename(file, file_id);

    uint8_t iv[8] = { 0x00 };
    char command[PM3_CMD_DATA_SIZE];
    uint8_t cmd[8];
    uint8_t data[21];
    uint8_t temp[8] = {0x0c, 0xa4, strtol(select_by, NULL, 16), 0x0c};

    int cmdlen = pad_block(temp, 4, cmd);
    int datalen = pad_block(file_id, 2, data);
    PrintAndLogEx(DEBUG, "cmd: %s", sprint_hex_inrow(cmd, cmdlen));
    PrintAndLogEx(DEBUG, "data: %s", sprint_hex_inrow(data, datalen));

    des3_encrypt_cbc(iv, kenc, data, datalen, temp);
    PrintAndLogEx(DEBUG, "temp: %s", sprint_hex_inrow(temp, datalen));
    uint8_t do87[11] = {0x87, 0x09, 0x01};
    memcpy(do87 + 3, temp, datalen);
    PrintAndLogEx(DEBUG, "do87: %s", sprint_hex_inrow(do87, datalen + 3));

    uint8_t m[19];
    memcpy(m, cmd, cmdlen);
    memcpy(m + cmdlen, do87, (datalen + 3));
    PrintAndLogEx(DEBUG, "m: %s", sprint_hex_inrow(m, datalen + cmdlen + 3));

    emrtd_bump_ssc(ssc);

    uint8_t n[27];
    memcpy(n, ssc, 8);
    memcpy(n + 8, m, (cmdlen + datalen + 3));
    PrintAndLogEx(DEBUG, "n: %s", sprint_hex_inrow(n, (cmdlen + datalen + 11)));

    uint8_t cc[8];
    retail_mac(kmac, n, (cmdlen + datalen + 11), cc);
    PrintAndLogEx(DEBUG, "cc: %s", sprint_hex_inrow(cc, 8));

    uint8_t do8e[10] = {0x8E, 0x08};
    memcpy(do8e + 2, cc, 8);
    PrintAndLogEx(DEBUG, "do8e: %s", sprint_hex_inrow(do8e, 10));

    int lc = datalen + 3 + 10;
    PrintAndLogEx(DEBUG, "lc: %i", lc);

    memcpy(data, do87, datalen + 3);
    memcpy(data + (datalen + 3), do8e, 10);
    PrintAndLogEx(DEBUG, "data: %s", sprint_hex_inrow(data, lc));

    sprintf(command, "0C%s%s0C%02X%s00", EMRTD_SELECT, select_by, lc, sprint_hex_inrow(data, lc));
    PrintAndLogEx(DEBUG, "command: %s", command);

    if (emrtd_exchange_commands(command, response, &resplen, false, true, use_14b) == false) {
        return false;
    }

    return emrtd_check_cc(ssc, kmac, response, resplen);
}

static bool _emrtd_secure_read_binary(uint8_t *kmac, uint8_t *ssc, int offset, int bytes_to_read, uint8_t *dataout, int *dataoutlen, bool use_14b) {
    char command[54];
    uint8_t cmd[8];
    uint8_t data[21];
    uint8_t temp[8] = {0x0c, 0xb0};

    PrintAndLogEx(DEBUG, "kmac: %s", sprint_hex_inrow(kmac, 20));

    // Set p1 and p2
    temp[2] = (uint8_t)(offset >> 8);
    temp[3] = (uint8_t)(offset >> 0);

    int cmdlen = pad_block(temp, 4, cmd);
    PrintAndLogEx(DEBUG, "cmd: %s", sprint_hex_inrow(cmd, cmdlen));

    uint8_t do97[3] = {0x97, 0x01, bytes_to_read};

    uint8_t m[11];
    memcpy(m, cmd, 8);
    memcpy(m + 8, do97, 3);

    emrtd_bump_ssc(ssc);

    uint8_t n[19];
    memcpy(n, ssc, 8);
    memcpy(n + 8, m, 11);
    PrintAndLogEx(DEBUG, "n: %s", sprint_hex_inrow(n, 19));

    uint8_t cc[8];
    retail_mac(kmac, n, 19, cc);
    PrintAndLogEx(DEBUG, "cc: %s", sprint_hex_inrow(cc, 8));

    uint8_t do8e[10] = {0x8E, 0x08};
    memcpy(do8e + 2, cc, 8);
    PrintAndLogEx(DEBUG, "do8e: %s", sprint_hex_inrow(do8e, 10));

    int lc = 13;
    PrintAndLogEx(DEBUG, "lc: %i", lc);

    memcpy(data, do97, 3);
    memcpy(data + 3, do8e, 10);
    PrintAndLogEx(DEBUG, "data: %s", sprint_hex_inrow(data, lc));

    sprintf(command, "0C%s%04X%02X%s00", EMRTD_READ_BINARY, offset, lc, sprint_hex_inrow(data, lc));
    PrintAndLogEx(DEBUG, "command: %s", command);

    if (emrtd_exchange_commands(command, dataout, dataoutlen, false, true, use_14b) == false) {
        return false;
    }

    return emrtd_check_cc(ssc, kmac, dataout, *dataoutlen);
}

static bool _emrtd_secure_read_binary_decrypt(uint8_t *kenc, uint8_t *kmac, uint8_t *ssc, int offset, int bytes_to_read, uint8_t *dataout, int *dataoutlen, bool use_14b) {
    uint8_t response[500];
    uint8_t temp[500];
    int resplen, cutat = 0;
    uint8_t iv[8] = { 0x00 };

    if (_emrtd_secure_read_binary(kmac, ssc, offset, bytes_to_read, response, &resplen, use_14b) == false) {
        return false;
    }

    PrintAndLogEx(DEBUG, "secreadbindec, offset %i on read %i: encrypted: %s", offset, bytes_to_read, sprint_hex_inrow(response, resplen));

    cutat = ((int) response[1]) - 1;

    des3_decrypt_cbc(iv, kenc, response + 3, cutat, temp);
    memcpy(dataout, temp, bytes_to_read);
    PrintAndLogEx(DEBUG, "secreadbindec, offset %i on read %i: decrypted: %s", offset, bytes_to_read, sprint_hex_inrow(temp, cutat));
    PrintAndLogEx(DEBUG, "secreadbindec, offset %i on read %i: decrypted and cut: %s", offset, bytes_to_read, sprint_hex_inrow(dataout, bytes_to_read));
    *dataoutlen = bytes_to_read;
    return true;
}

static int emrtd_read_file(uint8_t *dataout, int *dataoutlen, uint8_t *kenc, uint8_t *kmac, uint8_t *ssc, bool use_secure, bool use_14b) {
    uint8_t response[EMRTD_MAX_FILE_SIZE];
    int resplen = 0;
    uint8_t tempresponse[500];
    int tempresplen = 0;
    int toread = 4;
    int offset = 0;

    if (use_secure) {
        if (_emrtd_secure_read_binary_decrypt(kenc, kmac, ssc, offset, toread, response, &resplen, use_14b) == false) {
            return false;
        }
    } else {
        if (_emrtd_read_binary(offset, toread, response, &resplen, use_14b) == false) {
            return false;
        }
    }

    int datalen = emrtd_get_asn1_data_length(response, resplen, 1);
    int readlen = datalen - (3 - emrtd_get_asn1_field_length(response, resplen, 1));
    offset = 4;

    while (readlen > 0) {
        toread = readlen;
        if (readlen > 118) {
            toread = 118;
        }

        if (use_secure) {
            if (_emrtd_secure_read_binary_decrypt(kenc, kmac, ssc, offset, toread, tempresponse, &tempresplen, use_14b) == false) {
                return false;
            }
        } else {
            if (_emrtd_read_binary(offset, toread, tempresponse, &tempresplen, use_14b) == false) {
                return false;
            }
        }

        memcpy(response + resplen, tempresponse, tempresplen);
        offset += toread;
        readlen -= toread;
        resplen += tempresplen;
    }

    memcpy(dataout, &response, resplen);
    *dataoutlen = resplen;
    return true;
}

static int emrtd_lds_determine_tag_length(uint8_t tag) {
    if ((tag == 0x5F) || (tag == 0x7F)) {
        return 2;
    }
    return 1;
}

static bool emrtd_lds_get_data_by_tag(uint8_t *datain, size_t datainlen, uint8_t *dataout, size_t *dataoutlen, int tag1, int tag2, bool twobytetag, bool entertoptag, size_t skiptagcount) {
    int offset = 0;
    int skipcounter = 0;

    if (entertoptag) {
        offset += emrtd_lds_determine_tag_length(*datain);
        offset += emrtd_get_asn1_field_length(datain, datainlen, offset);
    }

    int e_idlen = 0;
    int e_datalen = 0;
    int e_fieldlen = 0;
    while (offset < datainlen) {
        PrintAndLogEx(DEBUG, "emrtd_lds_get_data_by_tag, offset: %i, data: %X", offset, *(datain + offset));
        // Determine element ID length to set as offset on asn1datalength
        e_idlen = emrtd_lds_determine_tag_length(*(datain + offset));

        // Get the length of the element
        e_datalen = emrtd_get_asn1_data_length(datain + offset, datainlen - offset, e_idlen);

        // Get the length of the element's length
        e_fieldlen = emrtd_get_asn1_field_length(datain + offset, datainlen - offset, e_idlen);

        PrintAndLogEx(DEBUG, "emrtd_lds_get_data_by_tag, e_idlen: %02X, e_datalen: %02X, e_fieldlen: %02X", e_idlen, e_datalen, e_fieldlen);

        // If the element is what we're looking for, get the data and return true
        if (*(datain + offset) == tag1 && (!twobytetag || *(datain + offset + 1) == tag2)) {
            if (skipcounter < skiptagcount) {
                skipcounter += 1;
            } else if (datainlen > e_datalen) {
                *dataoutlen = e_datalen;
                memcpy(dataout, datain + offset + e_idlen + e_fieldlen, e_datalen);
                return true;
            } else {
                PrintAndLogEx(ERR, "error (emrtd_lds_get_data_by_tag) e_datalen out-of-bounds");
                return false;
            }
        }
        offset += e_idlen + e_datalen + e_fieldlen;
    }
    // Return false if we can't find the relevant element
    return false;
}

static bool emrtd_select_and_read(uint8_t *dataout, int *dataoutlen, const char *file, uint8_t *ks_enc, uint8_t *ks_mac, uint8_t *ssc, bool use_secure, bool use_14b) {
    if (use_secure) {
        if (emrtd_secure_select_file(ks_enc, ks_mac, ssc, EMRTD_P1_SELECT_BY_EF, file, use_14b) == false) {
            PrintAndLogEx(ERR, "Failed to secure select %s.", file);
            return false;
        }
    } else {
        if (emrtd_select_file(EMRTD_P1_SELECT_BY_EF, file, use_14b) == false) {
            PrintAndLogEx(ERR, "Failed to select %s.", file);
            return false;
        }
    }

    if (emrtd_read_file(dataout, dataoutlen, ks_enc, ks_mac, ssc, use_secure, use_14b) == false) {
        PrintAndLogEx(ERR, "Failed to read %s.", file);
        return false;
    }
    return true;
}

const uint8_t jpeg_header[4] = { 0xFF, 0xD8, 0xFF, 0xE0 };
const uint8_t jpeg2k_header[6] = { 0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50 };

static int emrtd_dump_ef_dg2(uint8_t *file_contents, size_t file_length) {
    int offset, datalen = 0;

    // This is a hacky impl that just looks for the image header. I'll improve it eventually.
    // based on mrpkey.py
    // Note: Doing file_length - 6 to account for the longest data we're checking.
    // Checks first byte before the rest to reduce overhead
    for (offset = 0; offset < file_length - 6; offset++) {
        if ((file_contents[offset] == 0xFF && memcmp(jpeg_header, file_contents + offset, 4) != 0) ||
                (file_contents[offset] == 0x00 && memcmp(jpeg2k_header, file_contents + offset, 6) != 0)) {
            datalen = file_length - offset;
            break;
        }
    }

    // If we didn't get any data, return false.
    if (datalen == 0) {
        return PM3_ESOFT;
    }

    saveFile(dg_table[EF_DG2].filename, file_contents[offset] == 0xFF ? ".jpg" : ".jp2", file_contents + offset, datalen);
    return PM3_SUCCESS;
}

static int emrtd_dump_ef_dg5(uint8_t *file_contents, size_t file_length) {
    uint8_t data[EMRTD_MAX_FILE_SIZE];
    size_t datalen = 0;

    // If we can't find image in EF_DG5, return false.
    if (emrtd_lds_get_data_by_tag(file_contents, file_length, data, &datalen, 0x5F, 0x40, true, true, 0) == false) {
        return PM3_ESOFT;
    }

    if (datalen < EMRTD_MAX_FILE_SIZE) {
        saveFile(dg_table[EF_DG5].filename, data[0] == 0xFF ? ".jpg" : ".jp2", data, datalen);
    } else {
        PrintAndLogEx(ERR, "error (emrtd_dump_ef_dg5) datalen out-of-bounds");
        return PM3_ESOFT;
    }
    return PM3_SUCCESS;
}

static int emrtd_dump_ef_dg7(uint8_t *file_contents, size_t file_length) {
    uint8_t data[EMRTD_MAX_FILE_SIZE];
    size_t datalen = 0;

    // If we can't find image in EF_DG7, return false.
    if (emrtd_lds_get_data_by_tag(file_contents, file_length, data, &datalen, 0x5F, 0x42, true, true, 0) == false) {
        return PM3_ESOFT;
    }

    if (datalen < EMRTD_MAX_FILE_SIZE) {
        saveFile(dg_table[EF_DG7].filename, data[0] == 0xFF ? ".jpg" : ".jp2", data, datalen);
    } else {
        PrintAndLogEx(ERR, "error (emrtd_dump_ef_dg7) datalen out-of-bounds");
        return PM3_ESOFT;
    }
    return PM3_SUCCESS;
}

static int emrtd_dump_ef_sod(uint8_t *file_contents, size_t file_length) {
    int fieldlen = emrtd_get_asn1_field_length(file_contents, file_length, 1);
    int datalen = emrtd_get_asn1_data_length(file_contents, file_length, 1);

    if (fieldlen + 1 > EMRTD_MAX_FILE_SIZE) {
        PrintAndLogEx(ERR, "error (emrtd_dump_ef_sod) fieldlen out-of-bounds");
        return PM3_SUCCESS;
    }

    saveFile(dg_table[EF_SOD].filename, ".p7b", file_contents + fieldlen + 1, datalen);
    return PM3_ESOFT;
}

static bool emrtd_dump_file(uint8_t *ks_enc, uint8_t *ks_mac, uint8_t *ssc, const char *file, const char *name, bool use_secure, bool use_14b) {
    uint8_t response[EMRTD_MAX_FILE_SIZE];
    int resplen = 0;

    if (emrtd_select_and_read(response, &resplen, file, ks_enc, ks_mac, ssc, use_secure, use_14b) == false) {
        return false;
    }

    PrintAndLogEx(INFO, "Read %s, len: %i.", name, resplen);
    PrintAndLogEx(DEBUG, "Contents (may be incomplete over 2k chars): %s", sprint_hex_inrow(response, resplen));
    saveFile(name, ".BIN", response, resplen);
    emrtd_dg_t *dg = emrtd_fileid_to_dg(file);
    if ((dg != NULL) && (dg->dumper != NULL)) {
        dg->dumper(response, resplen);
    }
    return true;
}

static void rng(int length, uint8_t *dataout) {
    // Do very very secure prng operations
    //for (int i = 0; i < (length / 4); i++) {
    //    num_to_bytes(prng_successor(msclock() + i, 32), 4, &dataout[i * 4]);
    //}
    memset(dataout, 0x00, length);
}

static bool emrtd_do_bac(char *documentnumber, char *dob, char *expiry, uint8_t *ssc, uint8_t *ks_enc, uint8_t *ks_mac, bool use_14b) {
    uint8_t response[EMRTD_MAX_FILE_SIZE] = { 0x00 };
    int resplen = 0;

    uint8_t rnd_ic[8] = { 0x00 };
    uint8_t kenc[50] = { 0x00 };
    uint8_t kmac[50] = { 0x00 };
    uint8_t k_icc[16] = { 0x00 };
    uint8_t S[32] = { 0x00 };

    uint8_t rnd_ifd[8], k_ifd[16];
    rng(8, rnd_ifd);
    rng(16, k_ifd);

    PrintAndLogEx(DEBUG, "doc............... " _GREEN_("%s"), documentnumber);
    PrintAndLogEx(DEBUG, "dob............... " _GREEN_("%s"), dob);
    PrintAndLogEx(DEBUG, "exp............... " _GREEN_("%s"), expiry);

    char documentnumbercd = emrtd_calculate_check_digit(documentnumber);
    char dobcd = emrtd_calculate_check_digit(dob);
    char expirycd = emrtd_calculate_check_digit(expiry);

    char kmrz[25];
    sprintf(kmrz, "%s%i%s%i%s%i", documentnumber, documentnumbercd, dob, dobcd, expiry, expirycd);
    PrintAndLogEx(DEBUG, "kmrz.............. " _GREEN_("%s"), kmrz);

    uint8_t kseed[20] = { 0x00 };
    sha1hash((unsigned char *)kmrz, strlen(kmrz), kseed);
    PrintAndLogEx(DEBUG, "kseed (sha1)...... %s ", sprint_hex_inrow(kseed, 16));

    emrtd_deskey(kseed, KENC_type, 16, kenc);
    emrtd_deskey(kseed, KMAC_type, 16, kmac);
    PrintAndLogEx(DEBUG, "kenc.............. %s", sprint_hex_inrow(kenc, 16));
    PrintAndLogEx(DEBUG, "kmac.............. %s", sprint_hex_inrow(kmac, 16));

    // Get Challenge
    if (emrtd_get_challenge(8, rnd_ic, &resplen, use_14b) == false) {
        PrintAndLogEx(ERR, "Couldn't get challenge.");
        return false;
    }
    PrintAndLogEx(DEBUG, "rnd_ic............ %s", sprint_hex_inrow(rnd_ic, 8));

    memcpy(S, rnd_ifd, 8);
    memcpy(S + 8, rnd_ic, 8);
    memcpy(S + 16, k_ifd, 16);

    PrintAndLogEx(DEBUG, "S................. %s", sprint_hex_inrow(S, 32));

    uint8_t iv[8] = { 0x00 };
    uint8_t e_ifd[32] = { 0x00 };

    des3_encrypt_cbc(iv, kenc, S, sizeof(S), e_ifd);
    PrintAndLogEx(DEBUG, "e_ifd............. %s", sprint_hex_inrow(e_ifd, 32));

    uint8_t m_ifd[8] = { 0x00 };

    retail_mac(kmac, e_ifd, 32, m_ifd);
    PrintAndLogEx(DEBUG, "m_ifd............. %s", sprint_hex_inrow(m_ifd, 8));

    uint8_t cmd_data[40];
    memcpy(cmd_data, e_ifd, 32);
    memcpy(cmd_data + 32, m_ifd, 8);

    // Do external authentication
    if (emrtd_external_authenticate(cmd_data, sizeof(cmd_data), response, &resplen, use_14b) == false) {
        PrintAndLogEx(ERR, "Couldn't do external authentication. Did you supply the correct MRZ info?");
        return false;
    }
    PrintAndLogEx(INFO, "External authentication with BAC successful.");

    uint8_t dec_output[32] = { 0x00 };
    des3_decrypt_cbc(iv, kenc, response, 32, dec_output);
    PrintAndLogEx(DEBUG, "dec_output........ %s", sprint_hex_inrow(dec_output, 32));

    if (memcmp(rnd_ifd, dec_output + 8, 8) != 0) {
        PrintAndLogEx(ERR, "Challenge failed, rnd_ifd does not match.");
        return false;
    }

    memcpy(k_icc, dec_output + 16, 16);

    // Calculate session keys
    for (int x = 0; x < 16; x++) {
        kseed[x] = k_ifd[x] ^ k_icc[x];
    }

    PrintAndLogEx(DEBUG, "kseed............ %s", sprint_hex_inrow(kseed, 16));

    emrtd_deskey(kseed, KENC_type, 16, ks_enc);
    emrtd_deskey(kseed, KMAC_type, 16, ks_mac);

    PrintAndLogEx(DEBUG, "ks_enc........ %s", sprint_hex_inrow(ks_enc, 16));
    PrintAndLogEx(DEBUG, "ks_mac........ %s", sprint_hex_inrow(ks_mac, 16));

    memcpy(ssc, rnd_ic + 4, 4);
    memcpy(ssc + 4, rnd_ifd + 4, 4);

    PrintAndLogEx(DEBUG, "ssc........... %s", sprint_hex_inrow(ssc, 8));

    return true;
}

static bool emrtd_connect(bool *use_14b) {
    // Try to 14a
    SendCommandMIX(CMD_HF_ISO14443A_READER, ISO14A_CONNECT | ISO14A_NO_DISCONNECT, 0, 0, NULL, 0);
    PacketResponseNG resp;
    bool failed_14a = false;
    if (!WaitForResponseTimeout(CMD_ACK, &resp, 2500)) {
        DropField();
        failed_14a = true;
    }

    if (failed_14a || resp.oldarg[0] == 0) {
        PrintAndLogEx(INFO, "No eMRTD spotted with 14a, trying 14b.");
        // If not 14a, try to 14b
        SendCommandMIX(CMD_HF_ISO14443B_COMMAND, ISO14B_CONNECT | ISO14B_SELECT_STD, 0, 0, NULL, 0);
        if (!WaitForResponseTimeout(CMD_HF_ISO14443B_COMMAND, &resp, 2500)) {
            PrintAndLogEx(INFO, "No eMRTD spotted with 14b, exiting.");
            return false;
        }

        if (resp.oldarg[0] != 0) {
            PrintAndLogEx(INFO, "No eMRTD spotted with 14b, exiting.");
            return false;
        }
        *use_14b = true;
    }
    return true;
}

static bool emrtd_do_auth(char *documentnumber, char *dob, char *expiry, bool BAC_available, bool *BAC, uint8_t *ssc, uint8_t *ks_enc, uint8_t *ks_mac, bool *use_14b) {
    uint8_t response[EMRTD_MAX_FILE_SIZE] = { 0x00 };
    int resplen = 0;

    // Select MRTD applet
    if (emrtd_select_file(EMRTD_P1_SELECT_BY_NAME, EMRTD_AID_MRTD, *use_14b) == false) {
        PrintAndLogEx(ERR, "Couldn't select the MRTD application.");
        return false;
    }

    // Select EF_COM
    if (emrtd_select_file(EMRTD_P1_SELECT_BY_EF, dg_table[EF_COM].fileid, *use_14b) == false) {
        *BAC = true;
        PrintAndLogEx(INFO, "Basic Access Control is enforced. Will attempt external authentication.");
    } else {
        *BAC = false;
        // Select EF_DG1
        emrtd_select_file(EMRTD_P1_SELECT_BY_EF, dg_table[EF_DG1].fileid, *use_14b);

        if (emrtd_read_file(response, &resplen, NULL, NULL, NULL, false, *use_14b) == false) {
            *BAC = true;
            PrintAndLogEx(INFO, "Basic Access Control is enforced. Will attempt external authentication.");
        } else {
            *BAC = false;
        }
    }

    // Do Basic Access Aontrol
    if (*BAC) {
        // If BAC isn't available, exit out and warn user.
        if (!BAC_available) {
            PrintAndLogEx(ERR, "This eMRTD enforces Basic Access Control, but you didn't supply MRZ data. Cannot proceed.");
            PrintAndLogEx(HINT, "Check out hf emrtd info/dump --help, supply data with -n -d and -e.");
            return false;
        }

        if (emrtd_do_bac(documentnumber, dob, expiry, ssc, ks_enc, ks_mac, *use_14b) == false) {
            return false;
        }
    }

    return true;
}

int dumpHF_EMRTD(char *documentnumber, char *dob, char *expiry, bool BAC_available) {
    uint8_t response[EMRTD_MAX_FILE_SIZE] = { 0x00 };
    int resplen = 0;
    uint8_t ssc[8] = { 0x00 };
    uint8_t ks_enc[16] = { 0x00 };
    uint8_t ks_mac[16] = { 0x00 };
    bool BAC = false;
    bool use_14b = false;

    // Select the eMRTD
    if (!emrtd_connect(&use_14b)) {
        DropField();
        return PM3_ESOFT;
    }

    // Dump EF_CardAccess (if available)
    if (!emrtd_dump_file(ks_enc, ks_mac, ssc, dg_table[EF_CardAccess].fileid, dg_table[EF_CardAccess].filename, BAC, use_14b)) {
        PrintAndLogEx(INFO, "Couldn't dump EF_CardAccess, card does not support PACE.");
        PrintAndLogEx(HINT, "This is expected behavior for cards without PACE, and isn't something to be worried about.");
    }

    // Authenticate with the eMRTD
    if (!emrtd_do_auth(documentnumber, dob, expiry, BAC_available, &BAC, ssc, ks_enc, ks_mac, &use_14b)) {
        DropField();
        return PM3_ESOFT;
    }

    // Select EF_COM
    if (!emrtd_select_and_read(response, &resplen, dg_table[EF_COM].fileid, ks_enc, ks_mac, ssc, BAC, use_14b)) {
        PrintAndLogEx(ERR, "Failed to read EF_COM.");
        DropField();
        return PM3_ESOFT;
    }
    PrintAndLogEx(INFO, "Read EF_COM, len: %i.", resplen);
    PrintAndLogEx(DEBUG, "Contents (may be incomplete over 2k chars): %s", sprint_hex_inrow(response, resplen));
    saveFile(dg_table[EF_COM].filename, ".BIN", response, resplen);

    uint8_t filelist[50];
    size_t filelistlen = 0;

    if (!emrtd_lds_get_data_by_tag(response, resplen, filelist, &filelistlen, 0x5c, 0x00, false, true, 0)) {
        PrintAndLogEx(ERR, "Failed to read file list from EF_COM.");
        DropField();
        return PM3_ESOFT;
    }

    PrintAndLogEx(DEBUG, "File List: %s", sprint_hex_inrow(filelist, filelistlen));
    // Add EF_SOD to the list
    filelist[filelistlen++] = 0x77;
    // Dump all files in the file list
    for (int i = 0; i < filelistlen; i++) {
        emrtd_dg_t *dg = emrtd_tag_to_dg(filelist[i]);
        if (dg == NULL) {
            PrintAndLogEx(INFO, "File tag not found, skipping: %02X", filelist[i]);
            continue;
        }
        PrintAndLogEx(DEBUG, "Current file: %s", dg->filename);
        if (!dg->pace && !dg->eac) {
            emrtd_dump_file(ks_enc, ks_mac, ssc, dg->fileid, dg->filename, BAC, use_14b);
        }
    }
    DropField();
    return PM3_SUCCESS;
}

static bool emrtd_compare_check_digit(char *datain, int datalen, char expected_check_digit) {
    char tempdata[90] = { 0x00 };
    memcpy(tempdata, datain, datalen);

    uint8_t check_digit = emrtd_calculate_check_digit(tempdata) + 0x30;
    bool res = check_digit == expected_check_digit;
    PrintAndLogEx(DEBUG, "emrtd_compare_check_digit, expected %c == %c calculated ( %s )"
                  , expected_check_digit
                  , check_digit
                  , (res) ? _GREEN_("ok") : _RED_("fail"));
    return res;
}

static bool emrtd_mrz_verify_check_digit(char *mrz, int offset, int datalen) {
    char tempdata[90] = { 0x00 };
    memcpy(tempdata, mrz + offset, datalen);
    return emrtd_compare_check_digit(tempdata, datalen, mrz[offset + datalen]);
}

static void emrtd_print_legal_sex(char *legal_sex) {
    char sex[12] = { 0x00 };
    switch (*legal_sex) {
        case 'M':
            strncpy(sex, "Male", 5);
            break;
        case 'F':
            strncpy(sex, "Female", 7);
            break;
        case '<':
            strncpy(sex, "Unspecified", 12);
            break;
    }
    PrintAndLogEx(SUCCESS, "Legal Sex Marker......: " _YELLOW_("%s"), sex);
}

static int emrtd_mrz_determine_length(char *mrz, int offset, int max_length) {
    int i;
    for (i = max_length; i >= 0; i--) {
        if (mrz[offset + i - 1] != '<') {
            break;
        }
    }
    return i;
}

static int emrtd_mrz_determine_separator(char *mrz, int offset, int max_length) {
    // Note: this function does not account for len=0
    int i;
    for (i = max_length - 1; i > 0; i--) {
        if (mrz[offset + i] == '<' && mrz[offset + i + 1] == '<') {
            break;
        }
    }
    return i;
}

static void emrtd_mrz_replace_pad(char *data, int datalen, char newchar) {
    for (int i = 0; i < datalen; i++) {
        if (data[i] == '<') {
            data[i] = newchar;
        }
    }
}

static void emrtd_print_optional_elements(char *mrz, int offset, int length, bool verify_check_digit) {
    int i = emrtd_mrz_determine_length(mrz, offset, length);

    // Only print optional elements if they're available
    if (i != 0) {
        PrintAndLogEx(SUCCESS, "Optional elements.....: " _YELLOW_("%.*s"), i, mrz + offset);
    }

    if (verify_check_digit && !emrtd_mrz_verify_check_digit(mrz, offset, length)) {
        PrintAndLogEx(SUCCESS, _RED_("Optional element check digit is invalid."));
    }
}

static void emrtd_print_document_number(char *mrz, int offset) {
    int i = emrtd_mrz_determine_length(mrz, offset, 9);

    PrintAndLogEx(SUCCESS, "Document Number.......: " _YELLOW_("%.*s"), i, mrz + offset);

    if (!emrtd_mrz_verify_check_digit(mrz, offset, 9)) {
        PrintAndLogEx(SUCCESS, _RED_("Document number check digit is invalid."));
    }
}

static void emrtd_print_name(char *mrz, int offset, int max_length, bool localized) {
    char final_name[100] = { 0x00 };
    int namelen = emrtd_mrz_determine_length(mrz, offset, max_length);
    int sep = emrtd_mrz_determine_separator(mrz, offset, namelen);

    // Account for mononyms
    if (sep != 0) {
        int firstnamelen = (namelen - (sep + 2));

        memcpy(final_name, mrz + offset + sep + 2, firstnamelen);
        final_name[firstnamelen] = ' ';
        memcpy(final_name + firstnamelen + 1, mrz + offset, sep);
    } else {
        memcpy(final_name, mrz + offset, namelen);
    }

    // Replace < characters with spaces
    emrtd_mrz_replace_pad(final_name, namelen, ' ');

    if (localized) {
        PrintAndLogEx(SUCCESS, "Legal Name (Localized): " _YELLOW_("%s"), final_name);
    } else {
        PrintAndLogEx(SUCCESS, "Legal Name............: " _YELLOW_("%s"), final_name);
    }
}

static void emrtd_mrz_convert_date(char *mrz, int offset, char *final_date, bool is_expiry, bool is_full, bool is_ascii) {
    char work_date[9] = { 0x00 };
    int len = is_full ? 8 : 6;

    // Copy the data to a working array in the right format
    if (!is_ascii) {
        memcpy(work_date, sprint_hex_inrow((uint8_t *)mrz + offset, len / 2), len);
    } else {
        memcpy(work_date, mrz + offset, len);
    }

    // Set offset to 0 as we've now copied data.
    offset = 0;

    if (is_full) {
        // If we get the full date, use the first two characters from that for year
        memcpy(final_date, work_date, 2);
        // and do + 2 on offset so that rest of code uses the right data
        offset += 2;
    } else {
        char temp_year[3] = { 0x00 };
        memcpy(temp_year, work_date, 2);
        // If it's > 20, assume 19xx.
        if (strtol(temp_year, NULL, 10) < 20 || is_expiry) {
            final_date[0] = '2';
            final_date[1] = '0';
        } else {
            final_date[0] = '1';
            final_date[1] = '9';
        }
    }

    memcpy(final_date + 2, work_date + offset, 2);
    final_date[4] = '-';
    memcpy(final_date + 5, work_date + offset + 2, 2);
    final_date[7] = '-';
    memcpy(final_date + 8, work_date + offset + 4, 2);
}

static void emrtd_print_dob(char *mrz, int offset, bool full, bool ascii) {
    char final_date[12] = { 0x00 };
    emrtd_mrz_convert_date(mrz, offset, final_date, false, full, ascii);

    PrintAndLogEx(SUCCESS, "Date of birth.........: " _YELLOW_("%s"), final_date);

    if (!full && !emrtd_mrz_verify_check_digit(mrz, offset, 6)) {
        PrintAndLogEx(SUCCESS, _RED_("Date of Birth check digit is invalid."));
    }
}

static void emrtd_print_expiry(char *mrz, int offset) {
    char final_date[12] = { 0x00 };
    emrtd_mrz_convert_date(mrz, offset, final_date, true, false, true);

    PrintAndLogEx(SUCCESS, "Date of expiry........: " _YELLOW_("%s"), final_date);

    if (!emrtd_mrz_verify_check_digit(mrz, offset, 6)) {
        PrintAndLogEx(SUCCESS, _RED_("Date of expiry check digit is invalid."));
    }
}

static void emrtd_print_issuance(char *data, bool ascii) {
    char final_date[12] = { 0x00 };
    emrtd_mrz_convert_date(data, 0, final_date, true, true, ascii);

    PrintAndLogEx(SUCCESS, "Date of issue.........: " _YELLOW_("%s"), final_date);
}

static void emrtd_print_personalization_timestamp(uint8_t *data) {
    char str_date[0x0F] = { 0x00 };
    strcpy(str_date, sprint_hex_inrow(data, 0x0E));
    char final_date[20] = { 0x00 };
    sprintf(final_date, "%.4s-%.2s-%.2s %.2s:%.2s:%.2s", str_date, str_date + 4, str_date + 6, str_date + 8, str_date + 10, str_date + 12);

    PrintAndLogEx(SUCCESS, "Personalization at....: " _YELLOW_("%s"), final_date);
}

static void emrtd_print_unknown_timestamp_5f85(uint8_t *data) {
    char final_date[20] = { 0x00 };
    sprintf(final_date, "%.4s-%.2s-%.2s %.2s:%.2s:%.2s", data, data + 4, data + 6, data + 8, data + 10, data + 12);

    PrintAndLogEx(SUCCESS, "Unknown timestamp 5F85: " _YELLOW_("%s"), final_date);
    PrintAndLogEx(HINT, "This is very likely the personalization timestamp, but it is using an undocumented tag.");
}

static int emrtd_print_ef_com_info(uint8_t *data, size_t datalen) {
    uint8_t filelist[50];
    size_t filelistlen = 0;
    int res = emrtd_lds_get_data_by_tag(data, datalen, filelist, &filelistlen, 0x5c, 0x00, false, true, 0);
    if (!res) {
        PrintAndLogEx(ERR, "Failed to read file list from EF_COM.");
        return PM3_ESOFT;
    }

    // List files in the file list
    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(INFO, "-------------------- " _CYAN_("EF_COM") " --------------------");
    for (int i = 0; i < filelistlen; i++) {
        emrtd_dg_t *dg = emrtd_tag_to_dg(filelist[i]);
        if (dg == NULL) {
            PrintAndLogEx(INFO, "File tag not found, skipping: %02X", filelist[i]);
            continue;
        }
        PrintAndLogEx(SUCCESS, "%-7s...............: " _YELLOW_("%s"), dg->filename, dg->desc);
    }
    return PM3_SUCCESS;
}

static int emrtd_print_ef_dg1_info(uint8_t *data, size_t datalen) {
    int td_variant = 0;

    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(INFO, "-------------------- " _CYAN_("EF_DG1") " --------------------");

    // MRZ on TD1 is 90 characters, 30 on each row.
    // MRZ on TD3 is 88 characters, 44 on each row.
    char mrz[90] = { 0x00 };
    size_t mrzlen = 0;

    if (!emrtd_lds_get_data_by_tag(data, datalen, (uint8_t *) mrz, &mrzlen, 0x5f, 0x1f, true, true, 0)) {
        PrintAndLogEx(ERR, "Failed to read MRZ from EF_DG1.");
        return PM3_ESOFT;
    }

    // Determine and print the document type
    if (mrz[0] == 'I' && mrz[1] == 'P') {
        td_variant = 1;
        PrintAndLogEx(SUCCESS, "Document Type.........: " _YELLOW_("Passport Card"));
    } else if (mrz[0] == 'I') {
        td_variant = 1;
        PrintAndLogEx(SUCCESS, "Document Type.........: " _YELLOW_("ID Card"));
    } else if (mrz[0] == 'P') {
        td_variant = 3;
        PrintAndLogEx(SUCCESS, "Document Type.........: " _YELLOW_("Passport"));
    } else {
        td_variant = 1;
        PrintAndLogEx(SUCCESS, "Document Type.........: " _YELLOW_("Unknown"));
        PrintAndLogEx(INFO, "Assuming ID-style MRZ.");
    }
    PrintAndLogEx(SUCCESS, "Document Form Factor..: " _YELLOW_("TD%i"), td_variant);

    // Print the MRZ
    if (td_variant == 1) {
        PrintAndLogEx(DEBUG, "MRZ Row 1: " _YELLOW_("%.30s"), mrz);
        PrintAndLogEx(DEBUG, "MRZ Row 2: " _YELLOW_("%.30s"), mrz + 30);
        PrintAndLogEx(DEBUG, "MRZ Row 3: " _YELLOW_("%.30s"), mrz + 60);
    } else if (td_variant == 3) {
        PrintAndLogEx(DEBUG, "MRZ Row 1: " _YELLOW_("%.44s"), mrz);
        PrintAndLogEx(DEBUG, "MRZ Row 2: " _YELLOW_("%.44s"), mrz + 44);
    }

    PrintAndLogEx(SUCCESS, "Issuing state.........: " _YELLOW_("%.3s"), mrz + 2);

    if (td_variant == 3) {
        // Passport form factor
        PrintAndLogEx(SUCCESS, "Nationality...........: " _YELLOW_("%.3s"), mrz + 44 + 10);
        emrtd_print_name(mrz, 5, 38, false);
        emrtd_print_document_number(mrz, 44);
        emrtd_print_dob(mrz, 44 + 13, false, true);
        emrtd_print_legal_sex(&mrz[44 + 20]);
        emrtd_print_expiry(mrz, 44 + 21);
        emrtd_print_optional_elements(mrz, 44 + 28, 14, true);

        // Calculate and verify composite check digit
        char composite_check_data[50] = { 0x00 };
        memcpy(composite_check_data, mrz + 44, 10);
        memcpy(composite_check_data + 10, mrz + 44 + 13, 7);
        memcpy(composite_check_data + 17, mrz + 44 + 21, 23);

        if (!emrtd_compare_check_digit(composite_check_data, 39, mrz[87])) {
            PrintAndLogEx(SUCCESS, _RED_("Composite check digit is invalid."));
        }
    } else if (td_variant == 1) {
        // ID form factor
        PrintAndLogEx(SUCCESS, "Nationality...........: " _YELLOW_("%.3s"), mrz + 30 + 15);
        emrtd_print_name(mrz, 60, 30, false);
        emrtd_print_document_number(mrz, 5);
        emrtd_print_dob(mrz, 30, false, true);
        emrtd_print_legal_sex(&mrz[30 + 7]);
        emrtd_print_expiry(mrz, 30 + 8);
        emrtd_print_optional_elements(mrz, 15, 15, false);
        emrtd_print_optional_elements(mrz, 30 + 18, 11, false);

        // Calculate and verify composite check digit
        if (!emrtd_compare_check_digit(mrz, 59, mrz[59])) {
            PrintAndLogEx(SUCCESS, _RED_("Composite check digit is invalid."));
        }
    }

    return PM3_SUCCESS;
}

static int emrtd_print_ef_dg11_info(uint8_t *data, size_t datalen) {
    uint8_t taglist[100] = { 0x00 };
    size_t taglistlen = 0;
    uint8_t tagdata[1000] = { 0x00 };
    size_t tagdatalen = 0;

    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(INFO, "-------------------- " _CYAN_("EF_DG11") " -------------------");

    if (!emrtd_lds_get_data_by_tag(data, datalen, taglist, &taglistlen, 0x5c, 0x00, false, true, 0)) {
        PrintAndLogEx(ERR, "Failed to read file list from EF_DG11.");
        return PM3_ESOFT;
    }

    for (int i = 0; i < taglistlen; i++) {
        emrtd_lds_get_data_by_tag(data, datalen, tagdata, &tagdatalen, taglist[i], taglist[i + 1], taglist[i] == 0x5f, true, 0);
        // Don't bother with empty tags
        if (tagdatalen == 0) {
            continue;
        }
        // Special behavior for two char tags
        if (taglist[i] == 0x5f) {
            switch (taglist[i + 1]) {
                case 0x0e:
                    emrtd_print_name((char *) tagdata, 0, tagdatalen, true);
                    break;
                case 0x0f:
                    emrtd_print_name((char *) tagdata, 0, tagdatalen, false);
                    break;
                case 0x10:
                    PrintAndLogEx(SUCCESS, "Personal Number.......: " _YELLOW_("%.*s"), tagdatalen, tagdata);
                    break;
                case 0x11:
                    // TODO: acc for < separation
                    PrintAndLogEx(SUCCESS, "Place of Birth........: " _YELLOW_("%.*s"), tagdatalen, tagdata);
                    break;
                case 0x42:
                    // TODO: acc for < separation
                    PrintAndLogEx(SUCCESS, "Permanent Address.....: " _YELLOW_("%.*s"), tagdatalen, tagdata);
                    break;
                case 0x12:
                    PrintAndLogEx(SUCCESS, "Telephone.............: " _YELLOW_("%.*s"), tagdatalen, tagdata);
                    break;
                case 0x13:
                    PrintAndLogEx(SUCCESS, "Profession............: " _YELLOW_("%.*s"), tagdatalen, tagdata);
                    break;
                case 0x14:
                    PrintAndLogEx(SUCCESS, "Title.................: " _YELLOW_("%.*s"), tagdatalen, tagdata);
                    break;
                case 0x15:
                    PrintAndLogEx(SUCCESS, "Personal Summary......: " _YELLOW_("%.*s"), tagdatalen, tagdata);
                    break;
                case 0x16:
                    saveFile("ProofOfCitizenship", tagdata[0] == 0xFF ? ".jpg" : ".jp2", tagdata, tagdatalen);
                    break;
                case 0x17:
                    // TODO: acc for < separation
                    PrintAndLogEx(SUCCESS, "Other valid TDs nums..: " _YELLOW_("%.*s"), tagdatalen, tagdata);
                    break;
                case 0x18:
                    PrintAndLogEx(SUCCESS, "Custody Information...: " _YELLOW_("%.*s"), tagdatalen, tagdata);
                    break;
                case 0x2b:
                    emrtd_print_dob((char *) tagdata, 0, true, tagdatalen != 4);
                    break;
                default:
                    PrintAndLogEx(SUCCESS, "Unknown Field %02X%02X....: %s", taglist[i], taglist[i + 1], sprint_hex_inrow(tagdata, tagdatalen));
                    break;
            }

            i += 1;
        } else {
            // TODO: Account for A0
            PrintAndLogEx(SUCCESS, "Unknown Field %02X......: %s", taglist[i], sprint_hex_inrow(tagdata, tagdatalen));
        }
    }
    return PM3_SUCCESS;
}

static int emrtd_print_ef_dg12_info(uint8_t *data, size_t datalen) {
    uint8_t taglist[100] = { 0x00 };
    size_t taglistlen = 0;
    uint8_t tagdata[1000] = { 0x00 };
    size_t tagdatalen = 0;

    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(INFO, "-------------------- " _CYAN_("EF_DG12") " -------------------");

    if (!emrtd_lds_get_data_by_tag(data, datalen, taglist, &taglistlen, 0x5c, 0x00, false, true, 0)) {
        PrintAndLogEx(ERR, "Failed to read file list from EF_DG12.");
        return PM3_ESOFT;
    }

    for (int i = 0; i < taglistlen; i++) {
        emrtd_lds_get_data_by_tag(data, datalen, tagdata, &tagdatalen, taglist[i], taglist[i + 1], taglist[i] == 0x5f, true, 0);
        // Don't bother with empty tags
        if (tagdatalen == 0) {
            continue;
        }
        // Special behavior for two char tags
        if (taglist[i] == 0x5f) {
            // Several things here are longer than the rest but I can't think of a way to shorten them
            // ...and I doubt many states are using them.
            switch (taglist[i + 1]) {
                case 0x19:
                    PrintAndLogEx(SUCCESS, "Issuing Authority.....: " _YELLOW_("%.*s"), tagdatalen, tagdata);
                    break;
                case 0x26:
                    emrtd_print_issuance((char *) tagdata, tagdatalen != 4);
                    break;
                case 0x1b:
                    PrintAndLogEx(SUCCESS, "Endorsements & Observations: " _YELLOW_("%.*s"), tagdatalen, tagdata);
                    break;
                case 0x1c:
                    PrintAndLogEx(SUCCESS, "Tax/Exit Requirements.: " _YELLOW_("%.*s"), tagdatalen, tagdata);
                    break;
                case 0x1d:
                    saveFile("FrontOfDocument", tagdata[0] == 0xFF ? ".jpg" : ".jp2", tagdata, tagdatalen);
                    break;
                case 0x1e:
                    saveFile("BackOfDocument", tagdata[0] == 0xFF ? ".jpg" : ".jp2", tagdata, tagdatalen);
                    break;
                case 0x55:
                    emrtd_print_personalization_timestamp(tagdata);
                    break;
                case 0x56:
                    PrintAndLogEx(SUCCESS, "Serial of Personalization System: " _YELLOW_("%.*s"), tagdatalen, tagdata);
                    break;
                case 0x85:
                    emrtd_print_unknown_timestamp_5f85(tagdata);
                    break;
                default:
                    PrintAndLogEx(SUCCESS, "Unknown Field %02X%02X....: %s", taglist[i], taglist[i + 1], sprint_hex_inrow(tagdata, tagdatalen));
                    break;
            }

            i += 1;
        } else {
            // TODO: Account for A0
            PrintAndLogEx(SUCCESS, "Unknown Field %02X......: %s", taglist[i], sprint_hex_inrow(tagdata, tagdatalen));
        }
    }
    return PM3_SUCCESS;
}

static int emrtd_ef_sod_extract_signatures(uint8_t *data, size_t datalen, uint8_t *dataout, size_t *dataoutlen) {
    uint8_t top[EMRTD_MAX_FILE_SIZE] = { 0x00 };
    uint8_t signeddata[EMRTD_MAX_FILE_SIZE] = { 0x00 };
    uint8_t emrtdsigcontainer[EMRTD_MAX_FILE_SIZE] = { 0x00 };
    uint8_t emrtdsig[EMRTD_MAX_FILE_SIZE] = { 0x00 };
    uint8_t emrtdsigtext[EMRTD_MAX_FILE_SIZE] = { 0x00 };
    size_t toplen, signeddatalen, emrtdsigcontainerlen, emrtdsiglen, emrtdsigtextlen = 0;

    if (!emrtd_lds_get_data_by_tag(data, datalen, top, &toplen, 0x30, 0x00, false, true, 0)) {
        PrintAndLogEx(ERR, "Failed to read top from EF_SOD.");
        return false;
    }

    PrintAndLogEx(DEBUG, "top: %s.", sprint_hex_inrow(top, toplen));

    if (!emrtd_lds_get_data_by_tag(top, toplen, signeddata, &signeddatalen, 0xA0, 0x00, false, false, 0)) {
        PrintAndLogEx(ERR, "Failed to read signedData from EF_SOD.");
        return false;
    }

    PrintAndLogEx(DEBUG, "signeddata: %s.", sprint_hex_inrow(signeddata, signeddatalen));

    // Do true on reading into the tag as it's a "sequence"
    if (!emrtd_lds_get_data_by_tag(signeddata, signeddatalen, emrtdsigcontainer, &emrtdsigcontainerlen, 0x30, 0x00, false, true, 0)) {
        PrintAndLogEx(ERR, "Failed to read eMRTDSignature container from EF_SOD.");
        return false;
    }

    PrintAndLogEx(DEBUG, "emrtdsigcontainer: %s.", sprint_hex_inrow(emrtdsigcontainer, emrtdsigcontainerlen));

    if (!emrtd_lds_get_data_by_tag(emrtdsigcontainer, emrtdsigcontainerlen, emrtdsig, &emrtdsiglen, 0xA0, 0x00, false, false, 0)) {
        PrintAndLogEx(ERR, "Failed to read eMRTDSignature from EF_SOD.");
        return false;
    }

    PrintAndLogEx(DEBUG, "emrtdsig: %s.", sprint_hex_inrow(emrtdsig, emrtdsiglen));

    // TODO: Not doing memcpy here, it didn't work, fix it somehow
    if (!emrtd_lds_get_data_by_tag(emrtdsig, emrtdsiglen, emrtdsigtext, &emrtdsigtextlen, 0x04, 0x00, false, false, 0)) {
        PrintAndLogEx(ERR, "Failed to read eMRTDSignature (text) from EF_SOD.");
        return false;
    }
    memcpy(dataout, emrtdsigtext, emrtdsigtextlen);
    *dataoutlen = emrtdsigtextlen;
    return PM3_SUCCESS;
}

static int emrtd_parse_ef_sod_hash_algo(uint8_t *data, size_t datalen, int *hashalgo) {
    uint8_t hashalgoset[64] = { 0x00 };
    size_t hashalgosetlen = 0;

    // We'll return hash algo -1 if we can't find anything
    *hashalgo = -1;

    if (!emrtd_lds_get_data_by_tag(data, datalen, hashalgoset, &hashalgosetlen, 0x30, 0x00, false, true, 0)) {
        PrintAndLogEx(ERR, "Failed to read hash algo set from EF_SOD.");
        return false;
    }

    PrintAndLogEx(DEBUG, "hash algo set: %s", sprint_hex_inrow(hashalgoset, hashalgosetlen));

    // If last two bytes are 05 00, ignore them.
    // https://wf.lavatech.top/ave-but-random/emrtd-data-quirks#EF_SOD
    if (hashalgoset[hashalgosetlen - 2] == 0x05 && hashalgoset[hashalgosetlen - 1] == 0x00) {
        hashalgosetlen -= 2;
    }

    for (int hashi = 0; hashalg_table[hashi].name != NULL; hashi++) {
        PrintAndLogEx(DEBUG, "trying: %s", hashalg_table[hashi].name);
        // We're only interested in checking if the length matches to avoid memory shenanigans
        if (hashalg_table[hashi].descriptorlen != hashalgosetlen) {
            PrintAndLogEx(DEBUG, "len mismatch: %i", hashalgosetlen);
            continue;
        }

        if (memcmp(hashalg_table[hashi].descriptor, hashalgoset, hashalgosetlen) == 0) {
            *hashalgo = hashi;
            return PM3_SUCCESS;
        }
    }

    PrintAndLogEx(ERR, "Failed to parse hash list (Unknown algo: %s). Hash verification won't be available.", sprint_hex_inrow(hashalgoset, hashalgosetlen));
    return PM3_ESOFT;
}

static int emrtd_parse_ef_sod_hashes(uint8_t *data, size_t datalen, uint8_t *hashes, int *hashalgo) {
    uint8_t emrtdsig[EMRTD_MAX_FILE_SIZE] = { 0x00 };
    uint8_t hashlist[EMRTD_MAX_FILE_SIZE] = { 0x00 };
    uint8_t hash[64] = { 0x00 };
    size_t hashlen = 0;

    uint8_t hashidstr[4] = { 0x00 };
    size_t hashidstrlen = 0;

    size_t emrtdsiglen = 0;
    size_t hashlistlen = 0;
    size_t e_datalen = 0;
    size_t e_fieldlen = 0;
    size_t offset = 0;

    if (emrtd_ef_sod_extract_signatures(data, datalen, emrtdsig, &emrtdsiglen) != PM3_SUCCESS) {
        return false;
    }

    PrintAndLogEx(DEBUG, "hash data: %s", sprint_hex_inrow(emrtdsig, emrtdsiglen));

    emrtd_parse_ef_sod_hash_algo(emrtdsig, emrtdsiglen, hashalgo);

    if (!emrtd_lds_get_data_by_tag(emrtdsig, emrtdsiglen, hashlist, &hashlistlen, 0x30, 0x00, false, true, 1)) {
        PrintAndLogEx(ERR, "Failed to read hash list from EF_SOD.");
        return false;
    }

    PrintAndLogEx(DEBUG, "hash list: %s", sprint_hex_inrow(hashlist, hashlistlen));

    while (offset < hashlistlen) {
        // Get the length of the element
        e_datalen = emrtd_get_asn1_data_length(hashlist + offset, hashlistlen - offset, 1);

        // Get the length of the element's length
        e_fieldlen = emrtd_get_asn1_field_length(hashlist + offset, hashlistlen - offset, 1);

        switch (hashlist[offset]) {
            case 0x30:
                emrtd_lds_get_data_by_tag(hashlist + offset + e_fieldlen + 1, e_datalen, hashidstr, &hashidstrlen, 0x02, 0x00, false, false, 0);
                emrtd_lds_get_data_by_tag(hashlist + offset + e_fieldlen + 1, e_datalen, hash, &hashlen, 0x04, 0x00, false, false, 0);
                if (hashlen <= 64) {
                    memcpy(hashes + (hashidstr[0] * 64), hash, hashlen);
                } else {
                    PrintAndLogEx(ERR, "error (emrtd_parse_ef_sod_hashes) hashlen out-of-bounds");
                }
                break;
        }
        // + 1 for length of ID
        offset += 1 + e_datalen + e_fieldlen;
    }

    return PM3_SUCCESS;
}

int infoHF_EMRTD(char *documentnumber, char *dob, char *expiry, bool BAC_available) {
    uint8_t response[EMRTD_MAX_FILE_SIZE] = { 0x00 };
    int resplen = 0;
    uint8_t ssc[8] = { 0x00 };
    uint8_t ks_enc[16] = { 0x00 };
    uint8_t ks_mac[16] = { 0x00 };
    bool BAC = false;
    bool use_14b = false;

    // Select the eMRTD
    if (!emrtd_connect(&use_14b)) {
        DropField();
        return PM3_ESOFT;
    }

    // Select and authenticate with the eMRTD
    bool auth_result = emrtd_do_auth(documentnumber, dob, expiry, BAC_available, &BAC, ssc, ks_enc, ks_mac, &use_14b);

    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(INFO, "------------------ " _CYAN_("Basic Info") " ------------------");
    PrintAndLogEx(SUCCESS, "Communication standard: %s", use_14b ? _YELLOW_("ISO/IEC 14443(B)") : _YELLOW_("ISO/IEC 14443(A)"));
    PrintAndLogEx(SUCCESS, "BAC...................: %s", BAC ? _GREEN_("Enforced") : _RED_("Not enforced"));
    PrintAndLogEx(SUCCESS, "Authentication result.: %s", auth_result ? _GREEN_("Successful") : _RED_("Failed"));

    if (!auth_result) {
        DropField();
        return PM3_ESOFT;
    }

    if (!emrtd_select_and_read(response, &resplen, dg_table[EF_COM].fileid, ks_enc, ks_mac, ssc, BAC, use_14b)) {
        PrintAndLogEx(ERR, "Failed to read EF_COM.");
        DropField();
        return PM3_ESOFT;
    }

    int res = emrtd_print_ef_com_info(response, resplen);
    if (res != PM3_SUCCESS) {
        DropField();
        return res;
    }

    uint8_t filelist[50];
    size_t filelistlen = 0;

    if (!emrtd_lds_get_data_by_tag(response, resplen, filelist, &filelistlen, 0x5c, 0x00, false, true, 0)) {
        PrintAndLogEx(ERR, "Failed to read file list from EF_COM.");
        DropField();
        return PM3_ESOFT;
    }

    // Grab the hash list
    uint8_t dg_hashes[16][64];
    uint8_t hash_out[64];
    int hash_algo = 0;

    if (!emrtd_select_and_read(response, &resplen, dg_table[EF_SOD].fileid, ks_enc, ks_mac, ssc, BAC, use_14b)) {
        PrintAndLogEx(ERR, "Failed to read EF_SOD.");
        DropField();
        return PM3_ESOFT;
    }

    res = emrtd_parse_ef_sod_hashes(response, resplen, *dg_hashes, &hash_algo);
    if (res != PM3_SUCCESS) {
        PrintAndLogEx(ERR, "Failed to read hash list from EF_SOD. Hash checks will fail.");
    }

    // Dump all files in the file list
    for (int i = 0; i < filelistlen; i++) {
        emrtd_dg_t *dg = emrtd_tag_to_dg(filelist[i]);
        if (dg == NULL) {
            PrintAndLogEx(INFO, "File tag not found, skipping: %02X", filelist[i]);
            continue;
        }
        if (dg->fastdump && !dg->pace && !dg->eac) {
            if (emrtd_select_and_read(response, &resplen, dg->fileid, ks_enc, ks_mac, ssc, BAC, use_14b)) {
                if (dg->parser != NULL)
                    dg->parser(response, resplen);

                PrintAndLogEx(DEBUG, "EF_DG%i hash algo: %i", dg->dgnum, hash_algo);
                // Check file hash
                if (hash_algo != -1) {
                    PrintAndLogEx(DEBUG, "EF_DG%i hash on EF_SOD: %s", dg->dgnum, sprint_hex_inrow(dg_hashes[dg->dgnum], hashalg_table[hash_algo].hashlen));
                    hashalg_table[hash_algo].hasher(response, resplen, hash_out);
                    PrintAndLogEx(DEBUG, "EF_DG%i hash calc: %s", dg->dgnum, sprint_hex_inrow(hash_out, hashalg_table[hash_algo].hashlen));

                    if (memcmp(dg_hashes[dg->dgnum], hash_out, hashalg_table[hash_algo].hashlen) == 0) {
                        PrintAndLogEx(SUCCESS, _GREEN_("Hash verification passed for EF_DG%i."), dg->dgnum);
                    } else {
                        PrintAndLogEx(ERR, _RED_("Hash verification failed for EF_DG%i."), dg->dgnum);
                    }
                }
            }
        }
    }
    DropField();
    return PM3_SUCCESS;
}

int infoHF_EMRTD_offline(const char *path) {
    uint8_t *data;
    size_t datalen = 0;
    char *filepath = calloc(strlen(path) + 100, sizeof(char));
    if (filepath == NULL)
        return PM3_EMALLOC;
    strcpy(filepath, path);
    strncat(filepath, PATHSEP, 2);
    strcat(filepath, dg_table[EF_COM].filename);

    if (loadFile_safeEx(filepath, ".BIN", (void **)&data, (size_t *)&datalen, false) != PM3_SUCCESS) {
        PrintAndLogEx(ERR, "Failed to read EF_COM.");
        free(filepath);
        return PM3_ESOFT;
    }

    int res = emrtd_print_ef_com_info(data, datalen);
    if (res != PM3_SUCCESS) {
        free(data);
        free(filepath);
        return res;
    }

    uint8_t filelist[50];
    size_t filelistlen = 0;
    res = emrtd_lds_get_data_by_tag(data, datalen, filelist, &filelistlen, 0x5c, 0x00, false, true, 0);
    if (!res) {
        PrintAndLogEx(ERR, "Failed to read file list from EF_COM.");
        free(data);
        free(filepath);
        return PM3_ESOFT;
    }
    free(data);

    // Grab the hash list
    uint8_t dg_hashes[16][64];
    uint8_t hash_out[64];
    int hash_algo = 0;

    strcpy(filepath, path);
    strncat(filepath, PATHSEP, 2);
    strcat(filepath, dg_table[EF_SOD].filename);

    if (loadFile_safeEx(filepath, ".BIN", (void **)&data, (size_t *)&datalen, false) != PM3_SUCCESS) {
        PrintAndLogEx(ERR, "Failed to read EF_SOD.");
        free(filepath);
        return PM3_ESOFT;
    }

    res = emrtd_parse_ef_sod_hashes(data, datalen, *dg_hashes, &hash_algo);
    if (res != PM3_SUCCESS) {
        PrintAndLogEx(ERR, "Failed to read hash list from EF_SOD. Hash checks will fail.");
    }
    free(data);

    // Read files in the file list
    for (int i = 0; i < filelistlen; i++) {
        emrtd_dg_t *dg = emrtd_tag_to_dg(filelist[i]);
        if (dg == NULL) {
            PrintAndLogEx(INFO, "File tag not found, skipping: %02X", filelist[i]);
            continue;
        }
        if (!dg->pace && !dg->eac) {
            strcpy(filepath, path);
            strncat(filepath, PATHSEP, 2);
            strcat(filepath, dg->filename);
            if (loadFile_safeEx(filepath, ".BIN", (void **)&data, (size_t *)&datalen, false) == PM3_SUCCESS) {
                // we won't halt on parsing errors
                if (dg->parser != NULL)
                    dg->parser(data, datalen);

                PrintAndLogEx(DEBUG, "EF_DG%i hash algo: %i", dg->dgnum, hash_algo);
                // Check file hash
                if (hash_algo != -1) {
                    PrintAndLogEx(DEBUG, "EF_DG%i hash on EF_SOD: %s", dg->dgnum, sprint_hex_inrow(dg_hashes[dg->dgnum], hashalg_table[hash_algo].hashlen));
                    hashalg_table[hash_algo].hasher(data, datalen, hash_out);
                    PrintAndLogEx(DEBUG, "EF_DG%i hash calc: %s", dg->dgnum, sprint_hex_inrow(hash_out, hashalg_table[hash_algo].hashlen));

                    if (memcmp(dg_hashes[dg->dgnum], hash_out, hashalg_table[hash_algo].hashlen) == 0) {
                        PrintAndLogEx(SUCCESS, _GREEN_("Hash verification passed for EF_DG%i."), dg->dgnum);
                    } else {
                        PrintAndLogEx(ERR, _RED_("Hash verification failed for EF_DG%i."), dg->dgnum);
                    }
                }
                free(data);
            }
        }
    }
    free(filepath);
    return PM3_SUCCESS;
}

static void text_to_upper(uint8_t *data, int datalen) {
    // Loop over text to make lowercase text uppercase
    for (int i = 0; i < datalen; i++) {
        data[i] = toupper(data[i]);
    }
}

static bool validate_date(uint8_t *data, int datalen) {
    // Date has to be 6 chars
    if (datalen != 6) {
        return false;
    }

    // Check for valid date and month numbers
    char temp[4] = { 0x00 };
    memcpy(temp, data + 2, 2);
    int month = (int) strtol(temp, NULL, 10);
    memcpy(temp, data + 4, 2);
    int day = (int) strtol(temp, NULL, 10);

    return !(day <= 0 || day > 31 || month <= 0 || month > 12);
}

static int cmd_hf_emrtd_dump(const char *Cmd) {
    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf emrtd dump",
                  "Dump all files on an eMRTD",
                  "hf emrtd dump"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_str0("n", "documentnumber", "<alphanum>", "document number, up to 9 chars"),
        arg_str0("d", "dateofbirth", "<YYMMDD>", "date of birth in YYMMDD format"),
        arg_str0("e", "expiry", "<YYMMDD>", "expiry in YYMMDD format"),
        arg_str0("m", "mrz", "<[0-9A-Z<]>", "2nd line of MRZ, 44 chars"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);

    uint8_t mrz[45] = { 0x00 };
    uint8_t docnum[10] = { 0x00 };
    uint8_t dob[7] = { 0x00 };
    uint8_t expiry[7] = { 0x00 };
    bool BAC = true;
    bool error = false;
    int slen = 0;
    // Go through all args, if even one isn't supplied, mark BAC as unavailable
    if (CLIParamStrToBuf(arg_get_str(ctx, 1), docnum, 9, &slen) != 0 || slen == 0) {
        BAC = false;
    } else {
        text_to_upper(docnum, slen);
        if (slen != 9) {
            // Pad to 9 with <
            memset(docnum + slen, '<', 9 - slen);
        }
    }

    if (CLIParamStrToBuf(arg_get_str(ctx, 2), dob, 6, &slen) != 0 || slen == 0) {
        BAC = false;
    } else {
        if (!validate_date(dob, slen)) {
            PrintAndLogEx(ERR, "Date of birth date format is incorrect, cannot continue.");
            PrintAndLogEx(HINT, "Use the format YYMMDD.");
            error = true;
        }
    }

    if (CLIParamStrToBuf(arg_get_str(ctx, 3), expiry, 6, &slen) != 0 || slen == 0) {
        BAC = false;
    } else {
        if (!validate_date(expiry, slen)) {
            PrintAndLogEx(ERR, "Expiry date format is incorrect, cannot continue.");
            PrintAndLogEx(HINT, "Use the format YYMMDD.");
            error = true;
        }
    }

    if (CLIParamStrToBuf(arg_get_str(ctx, 4), mrz, 44, &slen) == 0 && slen != 0) {
        if (slen != 44) {
            PrintAndLogEx(ERR, "MRZ length is incorrect, it should be 44, not %i", slen);
            error = true;
        } else {
            BAC = true;
            text_to_upper(mrz, slen);
            memcpy(docnum, &mrz[0], 9);
            memcpy(dob,    &mrz[13], 6);
            memcpy(expiry, &mrz[21], 6);
            // TODO check MRZ checksums?
            if (!validate_date(dob, 6)) {
                PrintAndLogEx(ERR, "Date of birth date format is incorrect, cannot continue.");
                PrintAndLogEx(HINT, "Use the format YYMMDD.");
                error = true;
            }
            if (!validate_date(expiry, 6)) {
                PrintAndLogEx(ERR, "Expiry date format is incorrect, cannot continue.");
                PrintAndLogEx(HINT, "Use the format YYMMDD.");
                error = true;
            }
        }
    }

    CLIParserFree(ctx);
    if (error) {
        return PM3_ESOFT;
    }
    return dumpHF_EMRTD((char *)docnum, (char *)dob, (char *)expiry, BAC);
}

static int cmd_hf_emrtd_info(const char *Cmd) {
    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf emrtd info",
                  "Display info about an eMRTD",
                  "hf emrtd info"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_str0("n", "documentnumber", "<alphanum>", "document number, up to 9 chars"),
        arg_str0("d", "dateofbirth", "<YYMMDD>", "date of birth in YYMMDD format"),
        arg_str0("e", "expiry", "<YYMMDD>", "expiry in YYMMDD format"),
        arg_str0("m", "mrz", "<[0-9A-Z<]>", "2nd line of MRZ, 44 chars (passports only)"),
        arg_str0(NULL, "path", "<dirpath>", "display info from offline dump stored in dirpath"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);

    uint8_t mrz[45] = { 0x00 };
    uint8_t docnum[10] = { 0x00 };
    uint8_t dob[7] = { 0x00 };
    uint8_t expiry[7] = { 0x00 };
    bool BAC = true;
    bool error = false;
    int slen = 0;
    // Go through all args, if even one isn't supplied, mark BAC as unavailable
    if (CLIParamStrToBuf(arg_get_str(ctx, 1), docnum, 9, &slen) != 0 || slen == 0) {
        BAC = false;
    } else {
        text_to_upper(docnum, slen);
        if (slen != 9) {
            memset(docnum + slen, '<', 9 - slen);
        }
    }

    if (CLIParamStrToBuf(arg_get_str(ctx, 2), dob, 6, &slen) != 0 || slen == 0) {
        BAC = false;
    } else {
        if (!validate_date(dob, slen)) {
            PrintAndLogEx(ERR, "Date of birth date format is incorrect, cannot continue.");
            PrintAndLogEx(HINT, "Use the format YYMMDD.");
            error = true;
        }
    }

    if (CLIParamStrToBuf(arg_get_str(ctx, 3), expiry, 6, &slen) != 0 || slen == 0) {
        BAC = false;
    } else {
        if (!validate_date(expiry, slen)) {
            PrintAndLogEx(ERR, "Expiry date format is incorrect, cannot continue.");
            PrintAndLogEx(HINT, "Use the format YYMMDD.");
            error = true;
        }
    }

    if (CLIParamStrToBuf(arg_get_str(ctx, 4), mrz, 44, &slen) == 0 && slen != 0) {
        if (slen != 44) {
            PrintAndLogEx(ERR, "MRZ length is incorrect, it should be 44, not %i", slen);
            error = true;
        } else {
            BAC = true;
            text_to_upper(mrz, slen);
            memcpy(docnum, &mrz[0], 9);
            memcpy(dob,    &mrz[13], 6);
            memcpy(expiry, &mrz[21], 6);
            // TODO check MRZ checksums?
            if (!validate_date(dob, 6)) {
                PrintAndLogEx(ERR, "Date of birth date format is incorrect, cannot continue.");
                PrintAndLogEx(HINT, "Use the format YYMMDD.");
                error = true;
            }
            if (!validate_date(expiry, 6)) {
                PrintAndLogEx(ERR, "Expiry date format is incorrect, cannot continue.");
                PrintAndLogEx(HINT, "Use the format YYMMDD.");
                error = true;
            }
        }
    }
    uint8_t path[FILENAME_MAX] = { 0x00 };
    bool offline = CLIParamStrToBuf(arg_get_str(ctx, 5), path, sizeof(path), &slen) == 0 && slen > 0;
    CLIParserFree(ctx);
    if (error) {
        return PM3_ESOFT;
    }
    if (offline) {
        return infoHF_EMRTD_offline((const char *)path);
    } else {
        return infoHF_EMRTD((char *)docnum, (char *)dob, (char *)expiry, BAC);
    }
}

static int cmd_hf_emrtd_list(const char *Cmd) {
    char args[128] = {0};
    if (strlen(Cmd) == 0) {
        snprintf(args, sizeof(args), "-t 7816");
    } else {
        strncpy(args, Cmd, sizeof(args) - 1);
    }
    return CmdTraceList(args);
}

static command_t CommandTable[] = {
    {"help",    CmdHelp,           AlwaysAvailable, "This help"},
    {"dump",    cmd_hf_emrtd_dump, IfPm3Iso14443,   "Dump eMRTD files to binary files"},
    {"info",    cmd_hf_emrtd_info, AlwaysAvailable, "Display info about an eMRTD"},
    {"list",    cmd_hf_emrtd_list, AlwaysAvailable, "List ISO 14443A/7816 history"},
    {NULL, NULL, NULL, NULL}
};

static int CmdHelp(const char *Cmd) {
    (void)Cmd; // Cmd is not used so far
    CmdsHelp(CommandTable);
    return PM3_SUCCESS;
}

int CmdHFeMRTD(const char *Cmd) {
    clearCommandBuffer();
    return CmdsParse(CommandTable, Cmd);
}
