#include "modbus_core.h"
#include <time.h>

// Get current time in milliseconds (monotonic clock)
static uint64_t get_current_time_ms() 
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000;
}

// Calculate Modbus RTU CRC16 checksum
uint16_t modbus_crc16(const uint8_t* data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
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
    // Fix: remove redundant semicolon
    return (crc >> 8) | (crc << 8);
}

// Initialize Modbus parser context
void modbus_parser_init(ModbusParser* parser, uint32_t frame_timeout)
{
    if (!parser) return;
    memset(parser, 0, sizeof(ModbusParser));
    parser->state = MODBUS_STATE_IDLE;
    parser->frame_timeout = frame_timeout;
    parser->last_recv_time = get_current_time_ms();
}

// Get required data length based on Modbus function code (request/response)
static int get_modbus_data_len(uint8_t func_code, const uint8_t* data, int data_recv_len, int is_request)
{
    switch (func_code) {
        case MODBUS_FC_READ_HOLDING_REGISTERS:
            if (is_request) {
                return 4;
            } else {
                if (data_recv_len >= 1) {
                    return 1 + (data[0] * 2);
                } else {
                    return -1;
                }
            }
        case MODBUS_FC_WRITE_SINGLE_REGISTER:
            return 4;
        case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
            if (data_recv_len >= 5) {
                return 5 + data[4];
            } else if (data_recv_len == 0) {
                return -1;
            } else {
                return -1;
            }
        default: 
            return -2;
    }
}

// Parse single byte of Modbus RTU frame
int modbus_parse_rtu_byte(ModbusParser* parser, uint8_t byte, ModbusRTUFrame* frame)
{
    if (!parser || !frame) return -1;

    uint64_t now = get_current_time_ms();
    // Timeout: reset parser state
    if (now - parser->last_recv_time > parser->frame_timeout) {
        parser->state = MODBUS_STATE_IDLE;
        memset(&parser->rtu_frame, 0, sizeof(ModbusRTUFrame));
        parser->data_idx = 0;
    }
    parser->last_recv_time = now;

    switch (parser->state) {
        case MODBUS_STATE_IDLE:
            parser->rtu_frame.slave_addr = byte;
            parser->state = MODBUS_STATE_SLAVE_ADDR;
            break;
        case MODBUS_STATE_SLAVE_ADDR:
            parser->rtu_frame.func_code = byte;
            if (modbus_validate_func_code(byte) != 0) {
                parser->state = MODBUS_STATE_IDLE;
                return -2;
            }
            parser->state = MODBUS_STATE_FUNC_CODE;
            break;
        case MODBUS_STATE_FUNC_CODE:
            parser->rtu_frame.data[parser->data_idx++] = byte;
            // Fix: pass correct boolean (is_request) instead of func_code
            // Note: Here assume RTU frame is request (adjust if need to distinguish request/response)
            int need_data_len = get_modbus_data_len(parser->rtu_frame.func_code, parser->rtu_frame.data, parser->data_idx, 1);
            if (need_data_len == -2) {
                parser->state = MODBUS_STATE_IDLE;
                memset(&parser->rtu_frame, 0, sizeof(ModbusRTUFrame));
                parser->data_idx = 0;
                return -2;
            } else if (need_data_len == -1) {
                break;
            } else if (parser->data_idx >= need_data_len) {
                parser->rtu_frame.data_len = parser->data_idx;
                parser->state = MODBUS_STATE_CRC1;
            }
            break;
        case MODBUS_STATE_CRC1:
            parser->rtu_frame.crc = byte; // CRC low byte
            parser->state = MODBUS_STATE_CRC2;
            break;
        case MODBUS_STATE_CRC2:
            parser->rtu_frame.crc |= (byte << 8); // CRC high byte
            parser->state = MODBUS_STATE_COMPLETE;

            // Verify CRC checksum
            uint8_t check_data[MODBUS_MAX_FRAME_LEN];
            check_data[0] = parser->rtu_frame.slave_addr;
            check_data[1] = parser->rtu_frame.func_code;
            memcpy(check_data + 2, parser->rtu_frame.data, parser->rtu_frame.data_len);
            uint16_t calc_crc = modbus_crc16(check_data, 2 + parser->rtu_frame.data_len);
            if (calc_crc != parser->rtu_frame.crc) {
                parser->state = MODBUS_STATE_IDLE;
                memset(&parser->rtu_frame, 0, sizeof(ModbusRTUFrame));
                parser->data_idx = 0;
                return -3; // CRC mismatch
            }

            // Copy parsed frame to output
            memcpy(frame, &parser->rtu_frame, sizeof(ModbusRTUFrame));

            // Reset parser for next frame
            parser->state = MODBUS_STATE_IDLE;
            memset(&parser->rtu_frame, 0, sizeof(ModbusRTUFrame));
            parser->data_idx = 0;

            return 0; // Parse success
        default:
            // Fix: ensure state reset for unhandled cases
            parser->state = MODBUS_STATE_IDLE;
            memset(&parser->rtu_frame, 0, sizeof(ModbusRTUFrame));
            parser->data_idx = 0;
            break;  
    }
    return 1; // Frame not complete yet
}

// Convert Modbus RTU frame to TCP frame
int modbus_rtu_to_tcp(const ModbusRTUFrame* rtu_frame, uint16_t transaction_id, ModbusTCPFrame* tcp_frame)
{
    if (!rtu_frame || !tcp_frame) return -1;

    memset(tcp_frame, 0, sizeof(ModbusTCPFrame));

    tcp_frame->transaction_id = transaction_id;
    tcp_frame->protocol_id = 0;
    tcp_frame->slave_addr = rtu_frame->slave_addr;
    tcp_frame->func_code = rtu_frame->func_code;
    memcpy(tcp_frame->data, rtu_frame->data, rtu_frame->data_len);
    tcp_frame->data_len = rtu_frame->data_len;

    tcp_frame->length = 1 + 1 + rtu_frame->data_len; // slave_addr + func_code + data

    return 0;
}

// Convert Modbus TCP frame to RTU frame (calculate CRC)
int modbus_tcp_to_rtu(const ModbusTCPFrame* tcp_frame, ModbusRTUFrame* rtu_frame)
{
    if (!rtu_frame || !tcp_frame) return -1;

    memset(rtu_frame, 0, sizeof(ModbusRTUFrame));
    rtu_frame->slave_addr = tcp_frame->slave_addr;
    rtu_frame->func_code = tcp_frame->func_code;
    memcpy(rtu_frame->data, tcp_frame->data, tcp_frame->data_len);
    rtu_frame->data_len = tcp_frame->data_len;

    // Calculate CRC for RTU frame
    uint8_t crc_data[MODBUS_MAX_FRAME_LEN];
    crc_data[0] = rtu_frame->slave_addr;
    crc_data[1] = rtu_frame->func_code;
    memcpy(crc_data + 2, rtu_frame->data, rtu_frame->data_len);
    rtu_frame->crc = modbus_crc16(crc_data, 2 + rtu_frame->data_len);

    return 0;
}

// Validate Modbus function code
int modbus_validate_func_code(uint8_t func_code) {
    switch (func_code) {
        case MODBUS_FC_READ_HOLDING_REGISTERS:
        case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
        case MODBUS_FC_WRITE_SINGLE_REGISTER:
            return 0; // Valid
        default:
            return -1; // Invalid
    }
}

// Build Modbus RTU exception frame
int modbus_build_exception_rtu(uint8_t slave_addr, uint8_t func_code, uint8_t exception_code, ModbusRTUFrame* frame) {
    if (!frame) return -1;

    memset(frame, 0, sizeof(ModbusRTUFrame));
    frame->slave_addr = slave_addr;
    frame->func_code = func_code | 0x80; // Set highest bit for exception
    frame->data[0] = exception_code;
    frame->data_len = 1;

    // Calculate CRC for exception frame
    uint8_t crc_data[3];
    crc_data[0] = frame->slave_addr;
    crc_data[1] = frame->func_code;
    crc_data[2] = frame->data[0];
    frame->crc = modbus_crc16(crc_data, 3);

    return 0;
}
