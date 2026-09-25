FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );

/** @file
 *
 * Independent network link hold tests
 *
 */

#undef NDEBUG

#include <ipxe/netdevice.h>
#include <ipxe/timer.h>
#include <ipxe/test.h>
#include "netdev_test.h"

TESTNET ( linkblock, "02:00:00:00:00:01" );

/**
 * Test ownership, expiry, and lifecycle of link holds
 */
static void linkblock_test_exec ( void ) {
	struct net_device *netdev;
	unsigned long expiry;
	int refs;

	testnet_ok ( &linkblock );
	netdev = linkblock.netdev;
	ok ( ! netdev_link_blocked ( netdev ) );
	refs = netdev->refcnt.count;

	/* STP forwarding must not clear an EAP hold */
	netdev_link_block ( netdev, NETDEV_LINK_BLOCK_EAP, 45 * TICKS_PER_SEC );
	ok ( netdev->refcnt.count == ( refs + 1 ) );
	netdev_link_unblock ( netdev, NETDEV_LINK_BLOCK_STP );
	ok ( netdev_link_blocked ( netdev ) );
	ok ( netdev->refcnt.count == ( refs + 1 ) );
	expiry = netdev->link_block_expiry[NETDEV_LINK_BLOCK_EAP];

	/* A shorter hold and its release must preserve the longer hold */
	netdev_link_block ( netdev, NETDEV_LINK_BLOCK_STP, 4 * TICKS_PER_SEC );
	ok ( netdev->refcnt.count == ( refs + 1 ) );
	ok ( netdev->link_block.timeout <= 4 * TICKS_PER_SEC );
	netdev_link_unblock ( netdev, NETDEV_LINK_BLOCK_STP );
	ok ( netdev_link_blocked ( netdev ) );
	ok ( netdev->link_block_expiry[NETDEV_LINK_BLOCK_EAP] == expiry );
	ok ( netdev->link_block.timeout > 4 * TICKS_PER_SEC );

	/* LACP has independent ownership, including repeated clears */
	netdev_link_block ( netdev, NETDEV_LINK_BLOCK_LACP, 10 * TICKS_PER_SEC );
	netdev_link_unblock ( netdev, NETDEV_LINK_BLOCK_EAP );
	netdev_link_unblock ( netdev, NETDEV_LINK_BLOCK_EAP );
	ok ( netdev_link_blocked ( netdev ) );
	netdev_link_unblock ( netdev, NETDEV_LINK_BLOCK_LACP );
	ok ( ! netdev_link_blocked ( netdev ) );
	ok ( ! timer_running ( &netdev->link_block ) );
	ok ( netdev->refcnt.count == refs );

	/* Refresh one reason without changing another hold or its reference */
	netdev_link_block ( netdev, NETDEV_LINK_BLOCK_EAP, 45 * TICKS_PER_SEC );
	expiry = netdev->link_block_expiry[NETDEV_LINK_BLOCK_EAP];
	netdev_link_block ( netdev, NETDEV_LINK_BLOCK_STP, 4 * TICKS_PER_SEC );
	netdev->link_block_expiry[NETDEV_LINK_BLOCK_STP] = ( currticks() - 1 );
	netdev_link_block ( netdev, NETDEV_LINK_BLOCK_STP, 8 * TICKS_PER_SEC );
	ok ( netdev->link_block_expiry[NETDEV_LINK_BLOCK_EAP] == expiry );
	ok ( netdev->refcnt.count == ( refs + 1 ) );
	start_timer_nodelay ( &netdev->link_block );
	retry_poll();
	ok ( netdev->link_blocked == ( ( 1U << NETDEV_LINK_BLOCK_EAP ) |
				      ( 1U << NETDEV_LINK_BLOCK_STP ) ) );
	ok ( netdev->link_block.timeout > 4 * TICKS_PER_SEC );
	ok ( netdev->refcnt.count == ( refs + 1 ) );

	/* Expire STP through the real timer callback, retaining EAP */
	netdev->link_block_expiry[NETDEV_LINK_BLOCK_STP] = ( currticks() - 1 );
	start_timer_nodelay ( &netdev->link_block );
	retry_poll();
	ok ( netdev->link_blocked == ( 1U << NETDEV_LINK_BLOCK_EAP ) );
	ok ( timer_running ( &netdev->link_block ) );
	ok ( netdev->link_block_expiry[NETDEV_LINK_BLOCK_EAP] == expiry );
	ok ( netdev->refcnt.count == ( refs + 1 ) );
	netdev->link_block_expiry[NETDEV_LINK_BLOCK_EAP] = ( currticks() - 1 );
	start_timer_nodelay ( &netdev->link_block );
	retry_poll();
	ok ( ! netdev_link_blocked ( netdev ) );
	ok ( ! timer_running ( &netdev->link_block ) );
	ok ( netdev->refcnt.count == refs );

	/* An unchanged link notification must not release protocol holds */
	netdev_link_up ( netdev );
	netdev_link_block ( netdev, NETDEV_LINK_BLOCK_EAP, 45 * TICKS_PER_SEC );
	netdev_link_up ( netdev );
	ok ( netdev_link_blocked ( netdev ) );
	netdev_link_down ( netdev );
	ok ( ! netdev_link_blocked ( netdev ) );
	ok ( ! timer_running ( &netdev->link_block ) );
	netdev_link_up ( netdev );

	/* Closing a device releases all holds and their timer reference */
	netdev_link_block ( netdev, NETDEV_LINK_BLOCK_EAP, 45 * TICKS_PER_SEC );
	netdev_link_block ( netdev, NETDEV_LINK_BLOCK_LACP, 10 * TICKS_PER_SEC );
	testnet_close_ok ( &linkblock );
	ok ( ! netdev_link_blocked ( netdev ) );
	ok ( ! timer_running ( &netdev->link_block ) );
	testnet_open_ok ( &linkblock );
	ok ( ! netdev_link_blocked ( netdev ) );
	testnet_remove_ok ( &linkblock );
}

/** Independent link hold self-test */
struct self_test linkblock_test __self_test = {
	.name = "linkblock",
	.exec = linkblock_test_exec,
};
