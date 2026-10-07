"""认证和 SECURITY 后端委托测试；固定替身不具备密码学安全性。"""

import dlt698 as d
from dlt698 import _native as n
from dlt698.expert import SecurityBackend

from ..service.test_expert import Peers


class MockBackend(SecurityBackend):
    def __init__(self):
        super().__init__()
        self.opens = 0
        self.protects = 0
        self.resets = 0

    def begin_connect(self):
        return n.SymmetrySecurity(ciphertext=b"a", signature=b"b")

    def accept_connect(self, request):
        assert isinstance(request.mechanism, n.SymmetrySecurity)
        return n.AuthenticationResult(
            result=0, security=n.SecurityData(random=b"c", signature=b"d")
        )

    def verify_connect(self, request, response):
        assert response.security.signature == b"d"

    def protect_request(self, application):
        self.protects += 1
        return n.SecurityRequest(application=application, verification=n.Rn(value=b"c"))

    def open_request(self, message):
        self.opens += 1
        assert message.verification.value == b"c"
        return message.application

    def protect_response(self, application):
        self.protects += 1
        return n.SecurityResponse(application=application, verification=n.Mac(value=b"b"))

    def open_response(self, message):
        self.opens += 1
        assert message.verification.value == b"b"
        return message.application

    def reset(self):
        self.resets += 1


def test_authentication_and_protected_application_delegation():
    client_backend = MockBackend()
    server_backend = MockBackend()
    pair = Peers(
        public=True,
        client_options=d.SessionOptions(security_backend=client_backend),
        server_options=d.SessionOptions(security_backend=server_backend),
    )
    try:
        result = pair.wait(pair.client.get([d.Oad(oi=0x4500, attribute=2)])).message
        assert result.attributes[0].result.as_uint16() == 7
        assert client_backend.protects == client_backend.opens == 1
        assert server_backend.protects == server_backend.opens == 1
    finally:
        pair.close()
    assert client_backend.resets > 0
    assert server_backend.resets > 0
