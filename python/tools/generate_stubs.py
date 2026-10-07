"""从已安装扩展核对签名，再补 Python 输入协议与显式字段构造器。"""

from __future__ import annotations

import argparse
import ast
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def generate() -> str:
    output = ROOT / "build/python-stubs"
    subprocess.run(
        [
            sys.executable,
            "-m",
            "pybind11_stubgen",
            "dlt698._native",
            "-o",
            str(output),
            "--ignore-invalid-expressions",
            r"^<dlt698[.]_native[.][^>]+ object at 0x[0-9A-Fa-f]+>$",
            "--enum-class-locations",
            "ConnectionProfile:dlt698._native.ConnectionProfile",
            "--exit-code",
        ],
        check=True,
    )
    raw = (output / "dlt698/_native.pyi").read_text(encoding="utf-8")
    raw = raw.replace("dlt698._native.", "").replace("typing.Any", "object")
    raw = raw.replace("ConnectionProfile.ConnectionProfile.", "ConnectionProfile.")
    raw = re.sub(r"\blist\[", "builtins.list[", raw)
    tree = ast.parse(raw)
    tree.body.insert(2, ast.Import(names=[ast.alias(name="builtins")]))
    for node in tree.body:
        if not isinstance(node, ast.ClassDef):
            continue
        properties = {}
        writable = {
            method.name
            for method in node.body
            if isinstance(method, ast.FunctionDef)
            and any(
                isinstance(d, ast.Attribute) and d.attr == "setter" for d in method.decorator_list
            )
        }
        for method in node.body:
            if isinstance(method, ast.FunctionDef) and any(
                isinstance(d, ast.Name) and d.id == "property" for d in method.decorator_list
            ):
                properties[method.name] = method.returns
        for name in ("security_backend", "proxy"):
            if name in properties:
                nullable = ast.BinOp(
                    left=properties[name], op=ast.BitOr(), right=ast.Constant(value=None)
                )
                properties[name] = nullable
                for method in node.body:
                    if isinstance(method, ast.FunctionDef) and method.name == name:
                        if any(
                            isinstance(d, ast.Name) and d.id == "property"
                            for d in method.decorator_list
                        ):
                            method.returns = nullable
        for method in node.body:
            if not isinstance(method, ast.FunctionDef):
                continue
            if method.name == "__eq__" and node.name in {"Data", "Oad", "Omd"}:
                method.args.args[-1].annotation = ast.Name(id="object", ctx=ast.Load())
            if method.name == "__init__" and method.args.kwarg is not None:
                method.args.kwarg = None
                method.args.kwonlyargs = [
                    ast.arg(arg=k, annotation=v) for k, v in properties.items() if k in writable
                ]
                method.args.kw_defaults = [ast.Constant(value=Ellipsis) for _ in writable]
            if any(
                isinstance(d, ast.Attribute) and d.attr == "setter" for d in method.decorator_list
            ):
                method.args.args[-1].annotation = properties[method.name]
            if node.name == "Data":
                scalars = {
                    "boolean": "bool",
                    "float32": "float",
                    "float64": "float",
                    "visible_string": "str",
                    "utf8_string": "str",
                    "date_time": "collections.abc.Sequence[int]",
                    "date_time_s": "collections.abc.Sequence[int]",
                    "date": "collections.abc.Sequence[int]",
                    "time": "collections.abc.Sequence[int]",
                    **dict.fromkeys(
                        ("octet_string", "tsa", "mac", "rn"), "bytes | bytearray | memoryview"
                    ),
                    **dict.fromkeys(
                        (
                            "int8",
                            "int16",
                            "int32",
                            "int64",
                            "uint8",
                            "uint16",
                            "uint32",
                            "uint64",
                            "oi",
                            "enumeration",
                        ),
                        "int",
                    ),
                }
                if method.name in scalars:
                    method.args.args[0].annotation = ast.parse(
                        scalars[method.name], mode="eval"
                    ).body
                if method.name == "value":
                    union = (
                        "None | bool | int | float | str | bytes | builtins.list[Data] | "
                        "builtins.list[int] | Oad | Omd | Ti | ScalerUnit | Sid | SidMac | "
                        "Comdcb | BitString | Road | Region | SelectAll | Selector1 | Selector2 | "
                        "Selector3 | Selector4 | Selector5 | Selector6 | Selector7 | Selector8 | "
                        "Selector9 | Selector10 | NoMeters | AllMeters | MeterTypes | "
                        "MeterAddresses | MeterNumbers | MeterTypeRegions | "
                        "MeterAddressRegions | MeterNumberRegions | "
                        "builtins.list[Oad | Road]"
                    )
                    method.returns = ast.parse(union, mode="eval").body
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name == "build_info":
            node.returns = ast.parse("dict[str, str]", mode="eval").body
        if isinstance(node, ast.FunctionDef) and node.name in {
            "find_object",
            "find_attribute",
            "find_record",
        }:
            node.returns = ast.BinOp(
                left=node.returns, op=ast.BitOr(), right=ast.Constant(value=None)
            )
        if isinstance(node, ast.FunctionDef) and node.name in {
            "crc16",
            "decode_apdu",
            "decode_data",
            "decode_frame",
            "decode_security",
        }:
            node.args.args[0].annotation = ast.parse(
                "bytes | bytearray | memoryview", mode="eval"
            ).body
    for node in ast.walk(tree):
        if isinstance(node, ast.FunctionDef):
            args = node.args.posonlyargs + node.args.args
            for arg, default in zip(
                args[len(args) - len(node.args.defaults) :], node.args.defaults, strict=True
            ):
                if isinstance(default, ast.Constant) and default.value is None:
                    arg.annotation = ast.BinOp(left=arg.annotation, op=ast.BitOr(), right=default)
    ast.fix_missing_locations(tree)
    text = ast.unparse(tree) + "\n"
    text = text.replace(
        "__hash__: typing.ClassVar[None] = None",
        "__hash__: typing.ClassVar[None] = None  # type: ignore[assignment]",
    )
    lint = subprocess.run(
        [
            sys.executable,
            "-m",
            "ruff",
            "check",
            "--fix",
            "--ignore",
            "E501",
            "--stdin-filename",
            "_native.pyi",
        ],
        input=text,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if lint.returncode:
        raise RuntimeError(lint.stderr)
    text = lint.stdout
    process = subprocess.run(
        [sys.executable, "-m", "ruff", "format", "--stdin-filename", "_native.pyi"],
        input=text,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if process.returncode:
        raise RuntimeError(process.stderr)
    return process.stdout


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    path = ROOT / "python/src/dlt698/_native.pyi"
    text = generate()
    if args.check:
        if path.read_text(encoding="utf-8") != text:
            raise SystemExit("Native signatures/stubs differ; regenerate and review typed API")
    else:
        path.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
