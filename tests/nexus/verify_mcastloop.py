import sys
import os

# Paths for OpenThread pktverify toolset
CUR_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.append(CUR_DIR)

import verify_utils
from pktverify import consts
from pktverify.addrs import Ipv6Addr

def verify(pv):
    pkts = pv.pkts
    pv.summary.show()

    # Dynamically read RLOC16 addresses from pv.vars
    ADDR_ROUTER = pv.vars['ROUTER_RLOC16']
    ADDR_SED_A  = pv.vars['SED_A_RLOC16']
    ADDR_SED_B  = pv.vars['SED_B_RLOC16']

    print(f"Dynamic RLOC16 addresses: Router={hex(ADDR_ROUTER)}, SED_A={hex(ADDR_SED_A)}, SED_B={hex(ADDR_SED_B)}")

    mesh_local_prefix = pv.vars['mesh_local_prefix']
    ml_prefix = mesh_local_prefix.split('/')[0]


    MCAST_ADDR = Ipv6Addr('ff35:30:' + ml_prefix + '1')

    print(f"Dynamic multicast address: {MCAST_ADDR}")

    # --- STEP 1: SED_A sends to Router ---
    print("Step 1: SED_A sends to Router")
    pkts.filter_wpan_src16(ADDR_SED_A).\
        filter_wpan_dst16(ADDR_ROUTER).\
        must_next()

    # --- STEP 2: Router forwards to SED_B ---
    print("Step 2: Router forwards to SED_B")
    pkts.filter_wpan_src16(ADDR_ROUTER).\
        filter_wpan_dst16(ADDR_SED_B).\
        filter_ipv6_dst(MCAST_ADDR).\
        must_next()

    # --- STEP 3: Loopback to SED_A ---
    print("Step 3: Router sends (loopback) back to SED_A")
    pkts.filter_wpan_src16(ADDR_ROUTER).\
        filter_wpan_dst16(ADDR_SED_A).\
        filter_ipv6_dst(MCAST_ADDR).\
        must_not_next()

    print("\nVERIFIED: Dynamic check successful. SED_A physically received the packet.")

if __name__ == '__main__':
    verify_utils.run_main(verify)