"""Kiểu dữ liệu một mẫu đo; độc lập với Qt và cách truyền IPC."""
from dataclasses import dataclass


@dataclass(frozen=True)
class Telemetry:
    """Đơn vị: V, mA, mT, mm. power_on: 0/1; scale_code: 0..2."""
    voltage_v: float
    current_ma: float
    power_on: int
    magnetic_field_mt: float
    scale_code: int
    ruler_position_mm: int

