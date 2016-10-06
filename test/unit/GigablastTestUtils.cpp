#include <gtest/gtest.h>
#include "GigablastTestUtils.h"
#include "Loop.h"
#include "Collectiondb.h"
#include "Statsdb.h"
#include "Posdb.h"
#include "Titledb.h"
#include "Tagdb.h"
#include "Spider.h"
#include "Doledb.h"
#include "Clusterdb.h"
#include "Linkdb.h"

void GbTest::initializeRdbs() {
	ASSERT_TRUE(g_loop.init());

	ASSERT_TRUE(g_collectiondb.loadAllCollRecs());

	ASSERT_TRUE(g_statsdb.init());
	ASSERT_TRUE(g_posdb.init());
	ASSERT_TRUE(g_titledb.init());
	ASSERT_TRUE(g_tagdb.init());
	ASSERT_TRUE(g_spiderdb.init());
	ASSERT_TRUE(g_doledb.init());
	ASSERT_TRUE(g_spiderCache.init());
	ASSERT_TRUE(g_clusterdb.init());
	ASSERT_TRUE(g_linkdb.init());

	ASSERT_TRUE(g_collectiondb.addRdbBaseToAllRdbsForEachCollRec());
}

void GbTest::resetRdbs() {
	g_linkdb.reset();
	g_clusterdb.reset();
	g_spiderCache.reset();
	g_doledb.reset();
	g_spiderdb.reset();
	g_tagdb.reset();
	g_titledb.reset();
	g_posdb.reset();
	g_statsdb.reset();

	g_collectiondb.reset();

	g_loop.reset();
	new(&g_loop) Loop(); // some variables are not Loop::reset. Call the constructor to re-initialize them
}