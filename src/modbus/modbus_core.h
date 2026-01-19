#ifndef MODBUS_CORE_H
#define MODBUS_CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Global constants for Modbus protocol
#define MODBUS_MAX_FRAME_LEN 256
#define MODBUS_TCP_HEADER_LEN 6
#define MODBUS_CRC_LEN 2

// Modbus function codes (common types)
#define MODBUS_FC_READ_HOLDING_REGISTERS 0x03
#define MODBUS_FC_WRITE_SINGLE_REGISTER 0x06
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS 0x10

// Modbus TCP fixed parameters
#define MODBUS_TCP_TRANS_ID_H 0x00
#define MODBUS_TCP_TRANS_ID_L 0x01
#define MODBUS_TCP_PROTOCOL_ID 0x0000

// Modbus RTU frame structure (physical layer frame format)
typedef struct {
    uint8_t slave_addr;          // 从站地址(1-247)
    uint8_t func_code;           // 功能码
    uint8_t data[MODBUS_MAX_FRAME_LEN - 3]; // 数据域(排除地址/功能码/CRC)
    uint16_t data_len;           // 数据域长度
    uint16_t crc;                // CRC16校验和
} ModbusRTUFrame;

// Modbus TCP frame structure (MBAP header + PDU)
typedef struct {
    uint16_t transaction_id;     // 事务ID(请求唯一)
    uint16_t protocol_id;        // 协议ID(Modbus固定为0)
    uint16_t length;             // 剩余帧长度(从站+功能码+数据)
    uint8_t slave_addr;          // 从站地址
    uint8_t func_code;           // 功能码
    uint8_t data[MODBUS_MAX_FRAME_LEN - MODBUS_TCP_HEADER_LEN - 1]; // 数据域
    uint16_t data_len;           // 数据域长度
} ModbusTCPFrame;

// 核心函数声明
uint16_t modbus_crc16(const uint8_t* data, uint16_t len);
int modbus_parse_tcp_data(const uint8_t* tcp_data, uint16_t data_len, ModbusTCPFrame* tcp_frame);
int modbus_parse_rtu_data(const uint8_t* rtu_data, uint16_t data_len, ModbusRTUFrame* rtu_frame);
int modbus_rtu_to_tcp(const ModbusRTUFrame* rtu_frame, uint16_t transaction_id, ModbusTCPFrame* tcp_frame);
int modbus_tcp_to_rtu(const ModbusTCPFrame* tcp_frame, ModbusRTUFrame* rtu_frame);

#endif // !MODBUS_CORE_H