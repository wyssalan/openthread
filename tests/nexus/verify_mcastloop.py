import sys
import os

CUR_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.append(CUR_DIR)

import verify_utils
from pktverify import consts
from pktverify.addrs import Ipv6Addr


def verify(pv):
    pkts = pv.pkts
    pv.summary.show()

    # -----------------------------------------------------------------------
    # Adressen aus pv.vars lesen (werden von Nexus befüllt)
    # -----------------------------------------------------------------------
    ROUTER = pv.vars['ROUTER_RLOC16']
    SED_A  = pv.vars['SED_A_RLOC16']
    SED_B  = pv.vars['SED_B_RLOC16']

    # Die Multicast-Adresse muss zur Laufzeit aus dem Capture gelesen werden,
    # da der MeshLocal-Prefix dynamisch vergeben wird.
    # Wir leiten sie aus dem ersten UDP-Frame von SED_A ab.
    first_mcast_pkt = pkts.filter_wpan_src16(SED_A) \
                          .filter(lambda p: p.ipv6.dst.startswith("ff35")) \
                          .must_next()
    MCAST_ADDR = Ipv6Addr(first_mcast_pkt.ipv6.dst)
    print(f"[INFO] Dynamisch ermittelte Multicast-Adresse: {MCAST_ADDR}")
    print(f"[INFO] Router={hex(ROUTER)}, SED_A={hex(SED_A)}, SED_B={hex(SED_B)}")

    # -----------------------------------------------------------------------
    # STEP 1 – SED_A sendet INNER Frame an Router
    #
    # Das Inner Datagram ist der encapsulated MPL-Frame. SED_A schickt diesen
    # zuerst an seinen Parent (Router), damit der Router den lokalen Loop
    # bedienen kann. Erkennbar an: IPv6-Dst = Mcast-Addr, MPL-Option vorhanden,
    # aber kein äußeres IPv6-Tunnel-Header.
    # -----------------------------------------------------------------------
    print("\n--- STEP 1: SED_A sendet Inner Frame ---")
    pkts.filter_wpan_src16(SED_A) \
        .filter_wpan_dst16(ROUTER) \
        .filter_ipv6_dst(MCAST_ADDR) \
        .filter(lambda p: hasattr(p, 'ipv6.hopopts') or hasattr(p, 'mpl')) \
        .must_next()
    print("[OK] Inner Frame gefunden")

    # -----------------------------------------------------------------------
    # STEP 2 – SED_A sendet OUTER Frame an Router
    #
    # Das Outer Datagram ist der eigentliche MPL-Seed-Frame mit dem
    # 6LoWPAN-Mesh-Header. Kommt direkt nach dem Inner Frame vom selben Sender.
    # Ebenfalls SED_A → ROUTER, aber mit anderem Sequence-/Seed-Context.
    # -----------------------------------------------------------------------
    print("\n--- STEP 2: SED_A sendet Outer Frame ---")
    pkts.filter_wpan_src16(SED_A) \
        .filter_wpan_dst16(ROUTER) \
        .filter_ipv6_dst(MCAST_ADDR) \
        .must_next()  # zweites Auftreten nach dem must_next() in Step 1
    print("[OK] Outer Frame gefunden → SED_A hat 2× gesendet (Inner + Outer)")

    # -----------------------------------------------------------------------
    # STEP 3 – Router leitet an SED_B weiter
    #
    # Der Router forwarded das Multicast-Paket an seinen zweiten SED-Child.
    # MAC-Dst = SED_B, IPv6-Dst = Multicast-Adresse.
    # -----------------------------------------------------------------------
    print("\n--- STEP 3: Router leitet an SED_B weiter ---")
    pkts.filter_wpan_src16(ROUTER) \
        .filter_wpan_dst16(SED_B) \
        .filter_ipv6_dst(MCAST_ADDR) \
        .must_next()
    print("[OK] Router hat an SED_B weitergeleitet")

    # -----------------------------------------------------------------------
    # STEP 4 – Router sendet das Paket NICHT an SED_A zurück
    #
    # Da MulticastLoop auf Stack-Ebene erledigt wird (SED_A bekommt sein
    # eigenes Paket direkt vom lokalen Netif), darf der Router das Paket
    # NICHT nochmals an SED_A schicken → must_not_next()
    # -----------------------------------------------------------------------
    print("\n--- STEP 4: Kein Loopback-Frame Router→SED_A ---")
    pkts.filter_wpan_src16(ROUTER) \
        .filter_wpan_dst16(SED_A) \
        .filter_ipv6_dst(MCAST_ADDR) \
        .must_not_next()
    print("[OK] Kein unerwünschter Loopback-Frame vom Router an SED_A")

    print("\n✓ ALLE SCHRITTE VERIFIZIERT")
    print("  → SED_A hat 2× gesendet (Inner + Outer Frame)")
    print("  → Router hat an SED_B weitergeleitet")
    print("  → Kein doppelter Loopback via Router an SED_A")


if __name__ == '__main__':
    verify_utils.run_main(verify)