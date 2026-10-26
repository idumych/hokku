/*
 * Copyright 2013-2015 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <NetworkRoute.h>

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <new>
#include <vector>

#include <AutoDeleter.h>


// Linux's sockaddr has no sa_len: the length follows from the family.
static size_t
address_length(const sockaddr& address)
{
	switch (address.sa_family) {
		case AF_INET:
			return sizeof(sockaddr_in);
		case AF_INET6:
			return sizeof(sockaddr_in6);
		default:
			return sizeof(sockaddr);
	}
}


BNetworkRoute::BNetworkRoute()
{
	memset(&fRouteEntry, 0, sizeof(route_entry));
}


BNetworkRoute::~BNetworkRoute()
{
	UnsetDestination();
	UnsetMask();
	UnsetGateway();
	UnsetSource();
}


status_t
BNetworkRoute::SetTo(const BNetworkRoute& other)
{
	return SetTo(other.RouteEntry());
}


status_t
BNetworkRoute::SetTo(const route_entry& routeEntry)
{
	#define SET_ADDRESS(address, setFunction) \
		if (routeEntry.address != NULL) { \
			result = setFunction(*routeEntry.address); \
			if (result != B_OK) \
				return result; \
		}

	status_t result;
	SET_ADDRESS(destination, SetDestination)
	SET_ADDRESS(mask, SetMask)
	SET_ADDRESS(gateway, SetGateway)
	SET_ADDRESS(source, SetSource)

	SetFlags(routeEntry.flags);
	SetMTU(routeEntry.mtu);
	return B_OK;
}


void
BNetworkRoute::Adopt(BNetworkRoute& other)
{
	memcpy(&fRouteEntry, &other.fRouteEntry, sizeof(route_entry));
	memset(&other.fRouteEntry, 0, sizeof(route_entry));
}


const route_entry&
BNetworkRoute::RouteEntry() const
{
	return fRouteEntry;
}


const sockaddr*
BNetworkRoute::Destination() const
{
	return fRouteEntry.destination;
}


status_t
BNetworkRoute::SetDestination(const sockaddr& destination)
{
	return _AllocateAndSetAddress(destination, fRouteEntry.destination);
}


void
BNetworkRoute::UnsetDestination()
{
	_FreeAndUnsetAddress(fRouteEntry.destination);
}


const sockaddr*
BNetworkRoute::Mask() const
{
	return fRouteEntry.mask;
}


status_t
BNetworkRoute::SetMask(const sockaddr& mask)
{
	return _AllocateAndSetAddress(mask, fRouteEntry.mask);
}


void
BNetworkRoute::UnsetMask()
{
	_FreeAndUnsetAddress(fRouteEntry.mask);
}


const sockaddr*
BNetworkRoute::Gateway() const
{
	return fRouteEntry.gateway;
}


status_t
BNetworkRoute::SetGateway(const sockaddr& gateway)
{
	return _AllocateAndSetAddress(gateway, fRouteEntry.gateway);
}


void
BNetworkRoute::UnsetGateway()
{
	_FreeAndUnsetAddress(fRouteEntry.gateway);
}


const sockaddr*
BNetworkRoute::Source() const
{
	return fRouteEntry.source;
}


status_t
BNetworkRoute::SetSource(const sockaddr& source)
{
	return _AllocateAndSetAddress(source, fRouteEntry.source);
}


void
BNetworkRoute::UnsetSource()
{
	_FreeAndUnsetAddress(fRouteEntry.source);
}


uint32
BNetworkRoute::Flags() const
{
	return fRouteEntry.flags;
}


void
BNetworkRoute::SetFlags(uint32 flags)
{
	fRouteEntry.flags = flags;
}


uint32
BNetworkRoute::MTU() const
{
	return fRouteEntry.mtu;
}


void
BNetworkRoute::SetMTU(uint32 mtu)
{
	fRouteEntry.mtu = mtu;
}


int
BNetworkRoute::AddressFamily() const
{
	#define RETURN_FAMILY_IF_SET(address) \
		if (fRouteEntry.address != NULL \
			&& fRouteEntry.address->sa_family != AF_UNSPEC) { \
			return fRouteEntry.address->sa_family; \
		}

	RETURN_FAMILY_IF_SET(destination)
	RETURN_FAMILY_IF_SET(mask)
	RETURN_FAMILY_IF_SET(gateway)
	RETURN_FAMILY_IF_SET(source)

	return AF_UNSPEC;
}


status_t
BNetworkRoute::GetDefaultRoute(int family, const char* interfaceName,
	BNetworkRoute& route)
{
	BObjectList<BNetworkRoute, true> routes(1);
	status_t result = GetRoutes(family, interfaceName, RTF_DEFAULT, routes);
	if (result != B_OK)
		return result;

	if (routes.CountItems() == 0)
		return B_ENTRY_NOT_FOUND;

	route.Adopt(*routes.ItemAt(0));
	return B_OK;
}


status_t
BNetworkRoute::GetDefaultGateway(int family, const char* interfaceName,
	sockaddr& gateway)
{
	BNetworkRoute route;
	status_t result = GetDefaultRoute(family, interfaceName, route);
	if (result != B_OK)
		return result;

	const sockaddr* defaultGateway = route.Gateway();
	if (defaultGateway == NULL)
		return B_ENTRY_NOT_FOUND;

	memcpy(&gateway, defaultGateway, address_length(*defaultGateway));
	return B_OK;
}


status_t
BNetworkRoute::GetRoutes(int family, BObjectList<BNetworkRoute, true>& routes)
{
	return GetRoutes(family, NULL, 0, routes);
}


status_t
BNetworkRoute::GetRoutes(int family, const char* interfaceName,
	BObjectList<BNetworkRoute, true>& routes)
{
	return GetRoutes(family, interfaceName, 0, routes);
}


namespace {

struct LinuxRoute {
	char		interface[IF_NAMESIZE];
	sockaddr_storage destination;
	sockaddr_storage mask;
	sockaddr_storage gateway;
	bool		hasGateway;
	uint32		flags;
	uint32		mtu;
	uint32		metric;
};


void
set_ipv4(sockaddr_storage& storage, uint32 address)
{
	memset(&storage, 0, sizeof(storage));
	sockaddr_in& in = (sockaddr_in&)storage;
	in.sin_family = AF_INET;
	in.sin_addr.s_addr = address;
		// /proc/net/route prints the address as it is stored
}


bool
parse_ipv6(const char* hex, sockaddr_storage& storage)
{
	memset(&storage, 0, sizeof(storage));
	sockaddr_in6& in6 = (sockaddr_in6&)storage;
	in6.sin6_family = AF_INET6;
	for (int i = 0; i < 16; i++) {
		unsigned int byte;
		if (sscanf(hex + 2 * i, "%2x", &byte) != 1)
			return false;
		in6.sin6_addr.s6_addr[i] = (uint8)byte;
	}
	return true;
}


void
set_ipv6_mask(sockaddr_storage& storage, unsigned int prefixLength)
{
	memset(&storage, 0, sizeof(storage));
	sockaddr_in6& in6 = (sockaddr_in6&)storage;
	in6.sin6_family = AF_INET6;
	for (unsigned int i = 0; i < 16 && prefixLength > 0; i++) {
		unsigned int bits = std::min(prefixLength, 8u);
		in6.sin6_addr.s6_addr[i] = (uint8)(0xff << (8 - bits));
		prefixLength -= bits;
	}
}


// IPv4 routes from /proc/net/route:
// Iface Destination Gateway Flags RefCnt Use Metric Mask MTU Window IRTT
void
read_ipv4_routes(std::vector<LinuxRoute>& routes)
{
	FILE* file = fopen("/proc/net/route", "r");
	if (file == NULL)
		return;

	char line[256];
	if (fgets(line, sizeof(line), file) != NULL) {
		while (fgets(line, sizeof(line), file) != NULL) {
			char interface[IF_NAMESIZE + 1];
			unsigned int destination, gateway, flags, refCount, use, metric,
				mask, mtu;
			if (sscanf(line, "%16s %x %x %x %u %u %u %x %u", interface,
					&destination, &gateway, &flags, &refCount, &use, &metric,
					&mask, &mtu) != 9) {
				continue;
			}

			LinuxRoute route;
			memset(&route, 0, sizeof(route));
			strlcpy(route.interface, interface, sizeof(route.interface));
			set_ipv4(route.destination, destination);
			set_ipv4(route.mask, mask);
			set_ipv4(route.gateway, gateway);
			route.hasGateway = (flags & RTF_GATEWAY) != 0;
			route.flags = flags;
			if (destination == 0 && mask == 0)
				route.flags |= RTF_DEFAULT;
			route.mtu = mtu;
			route.metric = metric;
			routes.push_back(route);
		}
	}
	fclose(file);
}


// IPv6 routes from /proc/net/ipv6_route:
// dest dest-len src src-len next-hop metric refcnt use flags iface
void
read_ipv6_routes(std::vector<LinuxRoute>& routes)
{
	FILE* file = fopen("/proc/net/ipv6_route", "r");
	if (file == NULL)
		return;

	char line[256];
	while (fgets(line, sizeof(line), file) != NULL) {
		char destination[33], source[33], nextHop[33];
		char interface[IF_NAMESIZE + 1];
		unsigned int destinationLength, sourceLength, metric, refCount, use,
			flags;
		if (sscanf(line, "%32s %x %32s %x %32s %x %x %x %x %16s",
				destination, &destinationLength, source, &sourceLength,
				nextHop, &metric, &refCount, &use, &flags, interface) != 10) {
			continue;
		}
		if ((flags & RTF_REJECT) != 0)
			continue;

		LinuxRoute route;
		memset(&route, 0, sizeof(route));
		strlcpy(route.interface, interface, sizeof(route.interface));
		if (!parse_ipv6(destination, route.destination)
			|| !parse_ipv6(nextHop, route.gateway)) {
			continue;
		}
		set_ipv6_mask(route.mask, destinationLength);
		route.hasGateway = (flags & RTF_GATEWAY) != 0;
		route.flags = flags;
		if (destinationLength == 0)
			route.flags |= RTF_DEFAULT;
		route.metric = metric;
		routes.push_back(route);
	}
	fclose(file);
}

}	// namespace


status_t
BNetworkRoute::GetRoutes(int family, const char* interfaceName,
	uint32 filterFlags, BObjectList<BNetworkRoute, true>& routes)
{
	// Haiku asks the stack with SIOCGRTTABLE; Linux lists its routes in /proc.
	std::vector<LinuxRoute> found;
	if (family == AF_INET)
		read_ipv4_routes(found);
	else if (family == AF_INET6)
		read_ipv6_routes(found);
	else
		return B_NOT_SUPPORTED;

	// The preferred route first, so GetDefaultRoute() picks what the kernel uses
	std::stable_sort(found.begin(), found.end(),
		[](const LinuxRoute& a, const LinuxRoute& b) {
			return a.metric < b.metric;
		});

	for (size_t i = 0; i < found.size(); i++) {
		const LinuxRoute& linuxRoute = found[i];
		if ((interfaceName != NULL
				&& strcmp(linuxRoute.interface, interfaceName) != 0)
			|| (filterFlags != 0 && (linuxRoute.flags & filterFlags) == 0)) {
			continue;
		}

		route_entry entry;
		memset(&entry, 0, sizeof(entry));
		entry.destination = (sockaddr*)&linuxRoute.destination;
		entry.mask = (sockaddr*)&linuxRoute.mask;
		if (linuxRoute.hasGateway)
			entry.gateway = (sockaddr*)&linuxRoute.gateway;
		entry.flags = linuxRoute.flags;
		entry.mtu = linuxRoute.mtu;

		BNetworkRoute* route = new(std::nothrow) BNetworkRoute;
		if (route == NULL)
			return B_NO_MEMORY;

		status_t result = route->SetTo(entry);
		if (result != B_OK) {
			delete route;
			return result;
		}

		if (!routes.AddItem(route)) {
			delete route;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


status_t
BNetworkRoute::_AllocateAndSetAddress(const sockaddr& from,
	sockaddr*& to)
{

	if (to == NULL) {
		to = (sockaddr*)malloc(sizeof(sockaddr_storage));
		if (to == NULL)
			return B_NO_MEMORY;
	}

	memcpy(to, &from, address_length(from));
	return B_OK;
}


void
BNetworkRoute::_FreeAndUnsetAddress(sockaddr*& address)
{
	free(address);
	address = NULL;
}
