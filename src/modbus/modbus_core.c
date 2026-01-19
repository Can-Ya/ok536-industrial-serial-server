#include "modbus_core.h"

/**
 * Calculate Modbus RTU CRC16 checksum
 * @param data: Data buffer to calculate
 * @param len: Length of data buffer
 * @return CRC16 checksum value
 */
uint16_t modbus_crc16(const uint8_t* data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    if (data == NULL || len == 0) {
        fprintf(stderr, "[WARN] Modbus CRC16 input data is null or len is 0\n");
        return crc;
    }

    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }

    return (crc >> 8) | (crc << 8);
}

/**
 * Parse Modbus TCP data stream to TCP frame structure
 * @param tcp_data: Raw TCP data buffer
 * @param data_len: Length of TCP data buffer
 * @param tcp_frame: Output ModbusTCPFrame structure
 * @return 0 on success, -1/-2/-3/-4 on failure (see function comments)
 */

int modbus_parse_tcp_data(const uint8_t* tcp_data, uint16_t data_len, ModbusTCPFrame* tcp_frame)
{
    if (tcp_data == NULL || tcp_frame == NULL || data_len < MODBUS_TCP_HEADER_LEN) {
        fprintf(stderr, "[ERROR] Modbus TCP parse invalid params\n");
        return -1;
    }

    memset(tcp_frame, 0, sizeof(ModbusTCPFrame));

    tcp_frame->transaction_id = (tcp_data[0] << 8) | tcp_data[1];
    tcp_frame->protocol_id = (tcp_data[2] << 8) | tcp_data[3];
    tcp_frame->length = (tcp_data[4] << 8) | tcp_data[5];

    if (tcp_frame->protocol_id != MODBUS_TCP_PROTOCOL_ID) {
        fprintf(stderr, "[ERROR] Modbus TCP protocol ID is not 0\n");
        return -2;
    }

    if (tcp_frame->length + MODBUS_TCP_HEADER_LEN != data_len) {
        fprintf(stderr, "[ERROR] Modbus TCP frame length mismatch\n");
        return -3;
    }

    uint16_t pdu_offset = MODBUS_TCP_HEADER_LEN;
    tcp_frame->slave_addr = tcp_data[pdu_offset];
    tcp_frame->func_code = tcp_data[pdu_offset + 1];
    tcp_frame->data_len = tcp_frame->length - 2;

    if (tcp_frame->data_len > 0) {
        if (pdu_offset + 2 + tcp_frame->data_len > data_len) {
            fprintf(stderr, "[ERROR] Modbus TCP data out of bounds\n");
            return -4;
        }
        memcpy(tcp_frame->data, tcp_data + pdu_offset + 2, tcp_frame->data_len);
    }

    return 0;
}

/**
 * Parse Modbus RTU data stream to RTU frame structure
 * @param rtu_data: Raw RTU data buffer
 * @param data_len: Length of RTU data buffer
 * @param rtu_frame: Output ModbusRTUFrame structure
 * @return 0 on success, -1/-2 on failure (see function comments)
 */
int modbus_parse_rtu_data(const uint8_t* rtu_data, uint16_t data_len, ModbusRTUFrame* rtu_frame)
{
    if (rtu_data == NULL || rtu_frame == NULL || data_len < 4 || data_len > MODBUS_MAX_FRAME_LEN) {
        fprintf(stderr, "[ERROR] Modbus RTU parse invalid params\n");
        return -1;
    }

    memset(rtu_frame, 0, sizeof(ModbusRTUFrame));

    rtu_frame->slave_addr = rtu_data[0];
    rtu_frame->func_code = rtu_data[1];
    rtu_frame->data_len = data_len - 3; 
    rtu_frame->crc = (rtu_data[data_len - 2] << 8) | rtu_data[data_len - 1];

    if (rtu_frame->data_len > 0) {
        memcpy(rtu_frame->data, rtu_data + 2, rtu_frame->data_len);
    }

    uint16_t calc_crc = modbus_crc16(rtu_data, data_len - 2);
    if (calc_crc != rtu_frame->crc) {
        fprintf(stderr, "[ERROR] Modbus RTU CRC check failed (calc: 0x%04X, recv: 0x%04X)\n", calc_crc, rtu_frame->crc);
        return -2;
    }

    return 0;
}

/**
 * Convert Modbus RTU frame to TCP frame
 * @param rtu_frame: Input ModbusRTUFrame structure
 * @param transaction_id: TCP transaction ID
 * @param tcp_frame: Output ModbusTCPFrame structure
 * @return 0 on success, -1 on failure
 */
int modbus_rtu_to_tcp(const ModbusRTUFrame* rtu_frame, uint16_t transaction_id, ModbusTCPFrame* tcp_frame)
{
    if (rtu_frame == NULL || tcp_frame == NULL) {
        return -1;
    }

    memset(tcp_frame, 0, sizeof(ModbusTCPFrame));

    tcp_frame->transaction_id = transaction_id;
    tcp_frame->protocol_id = 0;
    tcp_frame->slave_addr = rtu_frame->slave_addr;
    tcp_frame->func_code = rtu_frame->func_code;
    tcp_frame->data_len = rtu_frame->data_len;
    memcpy(tcp_frame->data, rtu_frame->data, rtu_frame->data_len);
    tcp_frame->length = 1 + 1 + rtu_frame->data_len;

    return 0;
}

/**
 * Convert Modbus TCP frame to RTU frame (auto calculate CRC)
 * @param tcp_frame: Input ModbusTCPFrame structure
 * @param rtu_frame: Output ModbusRTUFrame structure
 * @return 0 on success, -1 on failure
 */
int modbus_tcp_to_rtu(const ModbusTCPFrame* tcp_frame, ModbusRTUFrame* rtu_frame)
{
    if (tcp_frame == NULL || rtu_frame == NULL) {
        return -1;
    }

    memset(rtu_frame, 0, sizeof(ModbusRTUFrame));

    rtu_frame->slave_addr = tcp_frame->slave_addr;
    rtu_frame->func_code = tcp_frame->func_code;
    rtu_frame->data_len = tcp_frame->data_len;
    memcpy(rtu_frame->data, tcp_frame->data, tcp_frame->data_len);

    uint8_t crc_data[MODBUS_MAX_FRAME_LEN];
    crc_data[0] = rtu_frame->slave_addr;
    crc_data[1] = rtu_frame->func_code;
    memcpy(crc_data + 2, rtu_frame->data, rtu_frame->data_len);
    rtu_frame->crc = modbus_crc16(crc_data, 2 + rtu_frame->data_len);

    return 0;
}