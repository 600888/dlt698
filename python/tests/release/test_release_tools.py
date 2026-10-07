"""联合发布门槛的缺失、篡改和不同提交拒绝测试。"""

import hashlib
import importlib
import io
import json
import tarfile
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[3]


@pytest.fixture
def joint(tmp_path, monkeypatch):
    monkeypatch.syspath_prepend(str(ROOT / "python/tools"))
    tools = importlib.import_module("joint_manifest")
    matrix = json.loads((ROOT / "python/release-matrix.json").read_text())
    identity = dict(
        version="1.2.3",
        commit="a" * 40,
        source_sha256="b" * 64,
        api_sha256="c" * 64,
        mode="release",
    )
    for platform in matrix["platforms"]:
        for linkage in matrix["linkages"]:
            name = f"dlt698-1.2.3-{platform['platform']}-{linkage}"
            path = tmp_path / (
                name + (".zip" if name.startswith("dlt698-1.2.3-windows") else ".tar.gz")
            )
            path.write_bytes(b"independent C++ artifact")
            record = {
                **identity,
                "filename": path.name,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
            (tmp_path / (path.name + ".provenance.json")).write_text(json.dumps(record))
        for minor in matrix["python"]:
            name = f"dlt698-1.2.3-cp{minor}-cp{minor}-{platform['wheel_platform']}.whl"
            with zipfile.ZipFile(tmp_path / name, "w") as wheel:
                wheel.writestr("dlt698/_build_info.json", json.dumps(identity))
                wheel.writestr("dlt698-1.2.3.dist-info/METADATA", "Version: 1.2.3\n")
                for required in (
                    "py.typed",
                    "_native.pyi",
                    "licenses/DLT698_LICENSE",
                    "licenses/PYBIND11_LICENSE",
                    "licenses/ASIO_LICENSE",
                    "_native.so",
                ):
                    wheel.writestr("dlt698/" + required, b"fixture")
    with tarfile.open(tmp_path / "dlt698-1.2.3.tar.gz", "w:gz") as archive:
        payload = json.dumps(identity).encode()
        entry = tarfile.TarInfo("dlt698-1.2.3/_source.json")
        entry.size = len(payload)
        archive.addfile(entry, io.BytesIO(payload))
    return tools, tmp_path, identity


def test_joint_release_accepts_only_complete_frozen_matrix(joint):
    tools, directory, identity = joint
    value = tools.combine(directory, identity["version"], identity["commit"])
    assert len(value["artifacts"]) == 31
    next(directory.glob("*.whl")).unlink()
    with pytest.raises(ValueError, match="incomplete"):
        tools.combine(directory, identity["version"], identity["commit"])


def test_joint_release_rejects_mixed_commit(joint):
    tools, directory, identity = joint
    record = next(directory.glob("*.provenance.json"))
    value = json.loads(record.read_text())
    value["commit"] = "d" * 40
    record.write_text(json.dumps(value))
    with pytest.raises(ValueError, match="mixed"):
        tools.combine(directory, identity["version"], identity["commit"])


def test_joint_release_rejects_altered_cpp_archive(joint):
    tools, directory, identity = joint
    next(directory.glob("*.zip")).write_bytes(b"changed")
    with pytest.raises(ValueError, match="recorded hash"):
        tools.combine(directory, identity["version"], identity["commit"])
