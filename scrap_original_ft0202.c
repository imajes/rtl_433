
    for (row = 0; row < bitbuffer->num_rows; ++row) {
        bitpos = 0;
        //
        // Find a preamble with enough bits after it that it could be a complete packet
        while (true)  {
            bitpos = bitbuffer_search(bitbuffer, row, 0, (const uint8_t *)&preamble_pattern, 12);
            bitpos = bitpos + 8 + 6 * 8;

            break if bitpos <= bitbuffer->bits_per_row[row];

            events += switchdoclabs_weather_decode(decoder, bitbuffer, row, bitpos + 8);

            if (events)
                return events; // for now, break after first successful message
            bitpos += 16;
        }
        bitpos = 0;

        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos, (const uint8_t *)&preamble_inverted, 12)) + 8 + 6 * 8 <= bitbuffer->bits_per_row[row]) {

            events += switchdoclabs_weather_decode(decoder, bitbuffer, row, bitpos + 8);

            if (events)
                return events; // for now, break after first successful message
            bitpos += 15;
        }
    }

    return events;



static int old_switchdoclabs_weather_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    const uint8_t preamble_pattern[2] = {0x01, 0x45}; // 12 bits
    const uint8_t preamble_inverted[2] = {0xfd, 0x45}; // 12 bits

    int row;
    unsigned bitpos;
    int events = 0;

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        bitpos = 0;
        //
        // Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos, (const uint8_t *)&preamble_pattern, 12)) + 8 + 6 * 8 <=bitbuffer->bits_per_row[row]) {

            events += switchdoclabs_weather_decode(decoder, bitbuffer, row, bitpos + 8);

            if (events)
                return events; // for now, break after first successful message
            bitpos += 16;
        }
        bitpos = 0;

        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos, (const uint8_t *)&preamble_inverted, 12)) + 8 + 6 * 8 <= bitbuffer->bits_per_row[row]) {

            events += switchdoclabs_weather_decode(decoder, bitbuffer, row, bitpos + 8);

            if (events)
                return events; // for now, break after first successful message
            bitpos += 15;
        }
    }

    return events;
}

static int switchdoclabs_weather_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    uint8_t b[16];
    data_t *data;
    int i;
    for (i = 0; i < 16; i++)
        b[i] = 0;


    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, 16 * 8);
    uint8_t sdlDevice;
    uint8_t sdlSerial;
    uint8_t sdlFlags;
    uint8_t sdlBatteryLow;
    uint16_t sdlAveWindSpeed;
    uint16_t sdlGust;
    uint16_t sdlWindDirection;
    uint32_t sdlCumulativeRain;
    uint8_t sdlSecondFlags;
    uint16_t sdlTemperature;
    uint8_t sdlHumidity;
    uint32_t sdlLight;
    uint16_t sdlUV;
    uint8_t sdlCRC;

    uint8_t b2[16];
    uint8_t sdlCalculated;

    b2[0] = 0xd4; // Shift all 4 bits fix b2[0];
    for (i = 0; i < 15; i++) {
        b2[i + 1] = ((b[i] & 0x0f) << 4) + ((b[i + 1] & 0xf0) >> 4);
        b2[i]     = b2[i + 1]; // shift 8
    }
    uint8_t expected = b2[13];

    sdlCalculated = 0; // GetCRC(0xc0, b2, 13); // crc8 from utils

    uint8_t calculated = sdlCalculated;

    if (expected != calculated) {
        decoder_logf(decoder, 1, __func__, "Checksum error in the message. Expected: %02x Calculated: %02x", expected, calculated);
        decoder_log_bitrow(decoder, 1, __func__, b, sizeof (b) * 8, "Checksum error");
        return 0;
    }

    sdlDevice = (b2[0] & 0xf0) >> 4;
    if (sdlDevice != 0x0c) {
        return 0; // not sdl device
    }

    sdlSerial         = (b2[0] & 0x0f) << 4 | (b2[1]) >> 4;
    sdlFlags          = b2[1] & 0x0f;
    sdlBatteryLow     = (sdlFlags & 0x08) >> 3;
    sdlAveWindSpeed   = b2[2] | ((sdlFlags & 0x01) << 8);
    sdlGust           = b2[3] | ((sdlFlags & 0x02) << 7);
    sdlWindDirection  = b2[4] | ((sdlFlags & 0x04) << 6);
    sdlCumulativeRain = (b2[5] << 8) + b2[6];
    sdlSecondFlags    = (b2[7] & 0xf0) >> 4;
    sdlTemperature    = ((b2[7] & 0x0f) << 8) + b2[8];
    sdlHumidity       = b2[9];
    sdlLight          = (b2[10] << 8) + b2[11] + ((sdlSecondFlags & 0x08) << 13);
    sdlLight          = 0x1FFFF & sdlLight;
    sdlUV             = b2[12];
    sdlCRC            = b2[13];

    if (sdlTemperature == 0xFF) {
        return 0; //  Bad Data
    }

    if (sdlAveWindSpeed == 0xFF) {
        return 0; //  Bad Data
    }

    /* clang-format off */
    data = data_make(
            "model",           "",                 DATA_STRING,   "SwitchDoc Labs-FT020T", "SwitchDoc Labs FT020T AIO",
            "id",              "Device",           DATA_INT,      sdlDevice,
            "id",              "Serial Number",    DATA_INT,      sdlSerial,
            "batterylow",      "Battery Low",      DATA_INT,      sdlBatteryLow,
            "avewindspeed",    "Ave Wind Speed",   DATA_INT,      sdlAveWindSpeed,
            "gustwindspeed",   "Gust",             DATA_INT,      sdlGust,
            "winddirection",   "Wind Direction",   DATA_INT,      sdlWindDirection,
            "cumulativerain",  "Cum Rain",         DATA_INT,      sdlCumulativeRain,
            "temperature",     "Temperature",      DATA_INT,      sdlTemperature,
            "humidity",        "Humidity",         DATA_INT,      sdlHumidity,
            "light",           "Light",            DATA_INT,      sdlLight,
            "uv",              "UV Index",         DATA_INT,      sdlUV,
            "mic",             "Integrity",        DATA_STRING,   "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}
