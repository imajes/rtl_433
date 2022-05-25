/** @file
 *  SwitchDoc Labs WeatherSense FT020T All In One Weather Sensor Pack
 *
 * Copyright (C) 2022 ....
 *
 */

/*

OOK modulated with Manchester encoding, halfbit-width 500 us.
Message length is 128 bits, every second time it will transmit two identical messages, packet gap 5400 us.

example raw messages:
{127}002986601422da0535087645fffa91b0
{128}0014c3300a11af029a843d22fffd4ac6

Integrity check is done using CRC8 using poly=0x31  init=0xc0

Message layout

    AAAA BBBBBBBB C D E F GGGGGGGG HHHHHHHH IIIIIIII JJJJ KKKKKKKKKKKK LLLL MMMMMMMMMMMM NNNNNNNN OOOOOOOOOOOOOOOO PPPPPPPP XXXXXXXX

- A : 4 bit: ?? Type code? part of id?, never seems to change
- B : 8 bit: Id, changes when reset
- C : 1 bit: Battery indicator 0 = Ok, 1 = Battery low
- D : 1 bit: MSB of Wind direction
- E : 1 bit: MSB of Wind Gust value
- F : 1 bit: MSB of Wind Avg value
- G : 8 bit: Wind Avg, scaled by 10
- H : 8 bit: Wind Gust, scaled by 10
- I : 8 bit: Wind direction in degrees.
- J : 4 bit: ? Might belong to the rain value
- K : 12 bit: Total rain in mm, scaled by 10
- L : 4 bit: Flag bitmask, always the same sequence: 1000
- M : 12 bit: Temperature in Fahrenheit, offset 400, scaled by 10
- N : 8 bit: Humidity
- O : 16 bit: Sunlight intensity, 0 to 200,000 lumens
- P : 8 bit: UV index (1-15)
- X : 8 bit: CRC, poly 0x31, init 0xc0
*/

// SDL NOTE
// three repeats without gap
// full preamble is 0x00145 (the last bits might not be fixed, e.g. 0x00146)
// and on decoding also 0xffd45

#include "decoder.h"

static int sdl_ft020t_decoder(r_device *decoder, bitbuffer_t *bitbuffer, unsigned int bitpos)
{
    uint8_t b[16]; // 128 bits = 16 bytes
    data_t *data;

    // pull and decode the message
    bitbuffer_extract_bytes(bitbuffer, 0, bitpos, b, 128);

    /* if (crc8(b, 16, 0x31, 0xc0)) { */
    /*     decoder_logf(decoder, 1, __func__, "CRC8 fail... "); */
    /*     return DECODE_FAIL_MIC; */
    /* } */

    uint8_t device_id         = (b[0] & 0xf0) >> 4;               // [0:4]
    uint8_t device_serial     = (b[0] & 0x0f) << 4 | (b[1] >> 4); // [0:4]
    uint8_t flags             = b[1] & 0x0f;
    uint8_t battery_low_state = (flags & 0x08) >> 3;
    uint16_t avg_wind_speed   = b[2] | ((flags & 0x01) << 8);
    uint16_t gust_speed       = b[3] | ((flags & 0x02) << 7);
    uint16_t wind_direction   = b[4] | ((flags & 0x04) << 6);
    uint32_t culm_rain        = (b[5] << 8) + b[6];
    uint8_t second_flags      = (b[7] & 0xf0) >> 4;
    uint16_t temp_raw         = ((b[7] & 0x0f) << 8) + b[8];
    uint8_t humidity          = b[9];
    uint32_t light_lux        = 0x1ffff & ((b[10] << 8) + b[11] + ((second_flags & 0x08) << 13));
    uint16_t uv               = b[12];
    uint8_t crc               = b[13];

    float temp_f = (temp_raw - 400) * 0.1f;
    float temp_c = (((temp_f - 32) * 5) / 9);

    decoder_logf(decoder, 2, __func__, "calculated second flags as %i", second_flags);
    decoder_logf(decoder, 2, __func__, "calculated flags as %i", flags);
    decoder_log(decoder, 2, __func__, "calculated the elements, now onto the make");

    /* clang-format off */
    data = data_make(
            "model",           "",                       DATA_STRING,   "SwitchDoc Labs-FT020T/AIO",
            "device_id",       "Device",                 DATA_INT,      device_id,
            "device_serial",   "Serial Number",          DATA_INT,      device_serial,
            "battery_low",     "Battery Low",            DATA_INT,      battery_low_state,
            "wind_avg_raw",    "Avg Wind Speed (raw)",   DATA_INT,      avg_wind_speed,
            "wind_avg_ms",     "Avg Wind Speed (m/s)",   DATA_FORMAT,   "%.1f m/s", DATA_DOUBLE, avg_wind_speed * 0.1f,
            "wind_gust_raw",   "Gust Speed (raw)",       DATA_INT,      gust_speed,
            "wind_gust_ms",    "Gust Speed (m/s)",       DATA_FORMAT,   "%.1f m/s", DATA_DOUBLE, gust_speed * 0.1f,
            "wind_dir_deg",    "Wind Direction (deg)",   DATA_INT,      wind_direction,
            "culm_rain_mm",    "Culm. Rain (mm)",        DATA_FORMAT,   "%.1f mm", DATA_DOUBLE, culm_rain * 0.1f,
            "temp_raw",        "Temperature",            DATA_INT,      temp_raw,
            "temp_F",          "Temperature (F)",        DATA_FORMAT,   "%.1f F", DATA_DOUBLE, temp_f,
            "temp_C",          "Temperature (c)",        DATA_FORMAT,   "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",        "Humidity (%)",           DATA_FORMAT,   "%u %%", DATA_INT, humidity,
            "light_lux",       "Light (lux)",            DATA_FORMAT,   "%u lux", DATA_INT, light_lux,
            "uv",              "UV Index",               DATA_INT,      uv,
            "mic",             "Integrity",              DATA_INT,      crc,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

/*
Battery Low: 0           Ave Wind Speed: 5         Gust      : 10            Wind Direction: 298       Cum Rain  : 666           Temperature: 1086         Humidity  : 35            Light     : 120009        UV Index  : 69
Integrity : CRC
[00] {127} 00 29 86 68 0a 14 54 05 35 08 7c 47 a9 92 8b 26
-------------------------------------------------------------------------------
Battery Low: 0           Ave Wind Speed: 5         Gust      : 10            Wind Direction: 298       Cum Rain  : 666           Temperature: 1086         Humidity  : 35            Light     : 120009        UV Index  : 69
Integrity : CRC
[00] {128} 00 14 c3 34 05 0a 2a 02 9a 84 3e 23 d4 c9 45 93
*/

static int sdl_ft020t_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    const uint8_t preamble_pattern[2] = {0x01, 0x45}; // 12 bits
    // const uint8_t preamble_inverted[2] = {0xfd, 0x45}; // 12 bits

    // too many rows returned, no bueno
    if (bitbuffer->num_rows > 1) {
        return DECODE_ABORT_EARLY;
    }

    // too small of a message returned, also no bueno
    if (bitbuffer->bits_per_row[0] < 127) {
        return DECODE_ABORT_LENGTH;
    }

    // too large of a message
    if (bitbuffer->bits_per_row[0] > 128) {
        return DECODE_ABORT_LENGTH;
    }

    decoder_log_bitbuffer(decoder, 2, __func__, bitbuffer, "");
    decoder_logf(decoder, 2, __func__, "num_rows: %i", bitbuffer->num_rows);
    decoder_logf(decoder, 2, __func__, "bits_per_row[0]: %i", bitbuffer->bits_per_row[0]);
    decoder_logf(decoder, 2, __func__, "bits_per_row[1]: %i", bitbuffer->bits_per_row[1]);

    unsigned pos = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, 12);
    pos += 12;
    decoder_logf(decoder, 2, __func__, "after preamble, we think we're at pos: %i", pos);

    if (pos > bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 2, __func__, "aborting because pos is larger than the size of the row");
        return DECODE_ABORT_LENGTH;
    }

    decoder_log(decoder, 2, __func__, "OK, good. it's about time to parse the buffer");
    sdl_ft020t_decoder(decoder, bitbuffer, pos);

    return 1;
}

static char *sdl_ft020t_output_fields[] = {
        "model",
        "device_id",
        "device_serial",
        "battery_low",
        "wind_avg_raw",
        "wind_avg_ms",
        "wind_gust_raw",
        "wind_gust_ms",
        "wind_dir_deg",
        "culm_rain_mm",
        "temp_raw",
        "temp_F",
        "temp_C",
        "humidity",
        "light_lux",
        "uv",
        "mic",
        NULL};

r_device switchdoclabs_FT020T = {
        .name        = "SwitchDoc Labs Weather FT020T Sensors",
        .code        = "sdl_ft020t",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 500,
        .long_width  = 0,    // not used
        .gap_limit   = 1200, // not used
        .reset_limit = 1200, // packet gap is 5400 us
        .decode_fn   = &sdl_ft020t_callback,
        .fields      = sdl_ft020t_output_fields};
