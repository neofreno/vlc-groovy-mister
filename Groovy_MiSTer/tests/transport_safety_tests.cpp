#include "../protocol/groovy_transport_safety.h"
#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    for (unsigned command = 0; command < 256; ++command) {
        const bool initOrVersion = command == 2 || command == 8;
        assert(groovy_safe::legacySessionAllows(uint8_t(command), false, false) == initOrVersion);
        assert(groovy_safe::legacySessionAllows(uint8_t(command), false, true) == initOrVersion);
        const bool control = initOrVersion || command == 1 || command == 3 || command == 5;
        assert(groovy_safe::legacySessionAllows(uint8_t(command), true, false) == control);
        assert(groovy_safe::legacySessionAllows(uint8_t(command), true, true) == (command >= 1 && command <= 8));
    }
    uint64_t tick=0; unsigned attempts=0;
    assert(!groovy_safe::waitUntil([&]{ ++attempts; return false; }, [&]{ return tick++; }, 10));
    assert(attempts==10);
    attempts=0;
    assert(groovy_safe::waitUntil([&]{ return ++attempts==3; }, [&]{ return tick++; }, 10));
    groovy_safe::FramePool<8,4096> pool; pool.init();
    const auto a=pool.take(), b=pool.take();
    assert(pool.release(a)); const auto c=pool.take();
    assert(c==a && c!=b); // Partial completion must not reuse pending B.
    assert(!pool.release(123)); assert(pool.release(b)); assert(!pool.release(b));
    assert(pool.release(c)); assert(pool.available==8);
    const auto rx=pool.take();
    assert(pool.validPacket(rx+256,1514));
    assert(pool.validPacket(rx+256,3840));
    assert(!pool.validPacket(rx+256,3841));
    assert(!pool.validPacket(8*4096,1));
    assert(pool.releasePacket(rx+256));
    assert(!pool.releasePacket(rx+256));
    assert(!pool.validPacket(rx+256,1));
    for (unsigned i=0;i<8;++i) assert(pool.take()!=UINT64_MAX);
    assert(pool.take()==UINT64_MAX);

    groovy_safe::LegacyTransfer transfer;
    std::vector<uint8_t> packet(1472,0xAB);
    assert(transfer.begin(230400,1658880,1472,100));
    for (int i=0;i<156;++i) assert(transfer.add(packet.data(),1472,110)==0);
    assert(!transfer.expire(179)); assert(transfer.expire(180));
    assert(!transfer.active()); // Lost final 768 bytes cannot hold the main loop.
    assert(transfer.begin(230400,1658880,1472,200));
    for (int i=0;i<156;++i) assert(transfer.add(packet.data(),1472,210)==0);
    assert(transfer.add(packet.data(),768,220)==1);
    for (auto v:transfer.data) assert(v==0xAB);
    assert(transfer.begin(2000,1658880,1472,300));
    assert(transfer.add(packet.data(),1000,301)==-1); // short/truncated
    assert(!transfer.begin(1658881,1658880,1472,400));
    assert(transfer.begin(100,32768,1472,400));
    assert(transfer.add(packet.data(),101,401)==-1); // destination overrun
    packet.resize(8972,0xAB);
    for (size_t chunk : {size_t(1472), size_t(3772), size_t(8972)}) {
        assert(transfer.begin(chunk*2+123,32768,1472,500));
        assert(transfer.add(packet.data(),chunk,501)==0);
        assert(transfer.add(packet.data(),chunk,502)==0);
        assert(transfer.add(packet.data(),123,503)==1);
    }

    uint8_t ethernet[64]{};
    ethernet[12]=8; ethernet[14]=0x45; ethernet[23]=17;
    ethernet[17]=28; ethernet[39]=8;
    groovy_safe::Datagram d;
    assert(groovy_safe::ethernetUDP(ethernet,42,d) && d.size==0);
    for (size_t size=0;size<42;++size) assert(!groovy_safe::ethernetUDP(ethernet,size,d));
    ethernet[39]=9; assert(!groovy_safe::ethernetUDP(ethernet,42,d));
    ethernet[39]=8; ethernet[20]=0x20; assert(!groovy_safe::ethernetUDP(ethernet,42,d));
    ethernet[20]=0; ethernet[14]=0x46; assert(!groovy_safe::ethernetUDP(ethernet,42,d));
    puts("PASS: XDP ownership/partial completions, packet bounds, legacy timeout/staging");
}
