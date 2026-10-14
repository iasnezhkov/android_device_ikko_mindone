import hashlib
import struct
import zlib


TARGET_SHA256 = "453912e5f79ba313986a8a6047a22f4d8033300f6db728f281a7f1c2ee92c37b"
TARGET_SIZE = 698052
TARGET_REGIONS = (
    (0x77000, 3728, 0x27),
    (0x2013400, 272, 0x07),
    (0x40100000, 9824, 0x07),
    (0xF02F0000, 520464, 0x80),
    (0xF0386000, 163392, 0x80),
)
RX_DROP_CALL = 0xF032A49C
RX_DROP_PREDICATE = 0xF032A398
RX_DESCRIPTOR_PARSER = 0xF030887C
MCAST_STATE_POINTER = 0xF03ADC2C
MCAST_STATE_ADDRESS = 0xF03AFF28


class TargetImage:
    def __init__(self, data):
        if not isinstance(data, bytes):
            raise ValueError("Immutable firmware bytes required")
        if len(data) != TARGET_SIZE:
            raise ValueError("Firmware length differs from the reviewed target")
        if hashlib.sha256(data).hexdigest() != TARGET_SHA256:
            raise ValueError("Firmware SHA256 differs from the reviewed target")
        if zlib.crc32(data[:-4]) != struct.unpack_from("<I", data, len(data) - 4)[0]:
            raise ValueError("Firmware image CRC32 mismatch")
        count = data[-34]
        if count != len(TARGET_REGIONS):
            raise ValueError("Firmware region count mismatch")
        tailers = len(data) - 36 - count * 40
        offset = 0
        self.regions = []
        for index, expected in enumerate(TARGET_REGIONS):
            crc, real, block, address, length, feature = struct.unpack_from(
                "<III4xIIB15x", data, tailers + index * 40
            )
            if (crc, real, block) != (0, 0, 0) or (address, length, feature) != expected:
                raise ValueError("Firmware region layout mismatch")
            if length > tailers - offset or address + length > 1 << 32:
                raise ValueError("Firmware region outside image or address space")
            self.regions.append((address, length, feature, offset))
            offset += length
        self.data = data
        if struct.unpack("<I", self.read(MCAST_STATE_POINTER, 4))[0] != MCAST_STATE_ADDRESS:
            raise ValueError("Firmware multicast state binding mismatch")

    def read(self, address, length):
        if not isinstance(address, int) or not isinstance(length, int) or length <= 0:
            raise ValueError("A positive integer address range is required")
        if address < 0 or address >= 1 << 32 or length > (1 << 32) - address:
            raise ValueError("Firmware address range overflow")
        for base, size, feature, offset in self.regions:
            if address >= base and address - base < size and length <= size - (address - base):
                if feature != 0x80:
                    raise ValueError("Encrypted firmware region is not decoded memory")
                return self.data[offset + address - base:offset + address - base + length]
        raise ValueError("Address range is not in a reviewed plaintext download region")
