# 测试向量来源

基线为 `plan/dlt69845-2017.pdf`。`.hex` 文件仅包含独立预期字节，不由待测 C++ 编码器生成。

| 文件 | 来源 | 人工核对的预期值 |
| --- | --- | --- |
| get-normal-request.hex | 附录 D.3.1，第 125 页（PDF 128） | PIID=1，OAD=40010200，TimeTag 不存在 |
| get-normal-response.hex | 同上 | 相同 OAD，Data=octet-string，6 字节地址，FollowReport/TimeTag 不存在 |
| get-list-request.hex | 附录 D.3.2，第 125 页 | PIID=2，OAD=20000200、20010200 |
| get-list-response.hex | 附录 D.3.2，第 125–126 页 | 电压 array/UInt16: 2413×3；电流 array/Int32: 1000×3 |

`core_test.cpp` 中另有按第 5 章组成的 GET 帧固定向量，其 APDU 使用 D.3.1、地址采用 D.1；CRC 按附录 A 的初值 FFFF、反射多项式 8408、输出异或 FFFF 独立计算。CRC 的 `123456789 → 906E` 用于验证算法。畸形输入向量明确检查错误类型，不只做往返测试。

后续扩展每个向量应记录标准版本、章节、原始值、字段预期、错误位置和复核状态。真实设备抓包另存，不能与规范向量混为一致性证据。
