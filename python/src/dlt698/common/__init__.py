"""common 分层公开接口；复用原生实现。"""

from .._native import (
    Error,
    ErrorCode,
    Limits,
)
from .errors import DarError, Dlt698Error

__all__ = ["Error", "ErrorCode", "Limits", "DarError", "Dlt698Error"]
