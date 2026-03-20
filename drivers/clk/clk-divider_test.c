// SPDX-License-Identifier: GPL-2.0
/*
 * Kunit tests for clk divider
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/units.h>

#include <kunit/clk.h>
#include <kunit/test.h>

/* 4 ought to be enough for anybody */
#define CLK_DUMMY_DIV_WIDTH 4
#define CLK_DUMMY_DIV_FLAGS (CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ROUND_CLOSEST)

struct clk_dummy_div {
	struct clk_hw hw;
	unsigned int div;
};

static unsigned long clk_dummy_div_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct clk_dummy_div *div = container_of(hw, struct clk_dummy_div, hw);

	return divider_recalc_rate(hw, parent_rate, div->div, NULL,
				   CLK_DUMMY_DIV_FLAGS, CLK_DUMMY_DIV_WIDTH);
}

static int clk_dummy_div_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req)
{
	if (!(clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) && req->best_parent_rate < req->rate)
		return -EINVAL;

	return divider_determine_rate(hw, req, NULL, CLK_DUMMY_DIV_WIDTH, CLK_DUMMY_DIV_FLAGS);
}

static int clk_dummy_div_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct clk_dummy_div *div = container_of(hw, struct clk_dummy_div, hw);

	div->div = divider_get_val(rate, parent_rate, NULL, CLK_DUMMY_DIV_WIDTH,
				   CLK_DUMMY_DIV_FLAGS);

	return 0;
}

static const struct clk_ops clk_dummy_div_ops = {
	.recalc_rate = clk_dummy_div_recalc_rate,
	.determine_rate = clk_dummy_div_determine_rate,
	.set_rate = clk_dummy_div_set_rate,
};

/*
 * clk-divider.c has support for v2 rate negotiation, and setting the parent
 * based on the LCM, however we need to be able to test just setting the parent
 * rate based on the LCM, and not set the v2 rate negotiation flag. This is to
 * demonstrate existing behavior in the clk core when a parent rate that's
 * suitable for all children is selected, a sibling will still have it's rate
 * negatively affected. Some boards may be unknowingly dependent on this
 * behavior, and we want to ensure this behavior stays the same.
 */
static int clk_dummy_div_lcm_determine_rate(struct clk_hw *hw,
					    struct clk_rate_request *req)
{
	struct clk_hw *parent_hw = clk_hw_get_parent(hw);

	if (!(clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) && req->best_parent_rate < req->rate)
		return -EINVAL;

	req->best_parent_rate = clk_hw_get_children_lcm(parent_hw, hw, req->rate);
	req->best_parent_hw = parent_hw;

	return divider_determine_rate(hw, req, NULL, CLK_DUMMY_DIV_WIDTH, CLK_DUMMY_DIV_FLAGS);
}

static const struct clk_ops clk_dummy_div_lcm_ops = {
	.recalc_rate = clk_dummy_div_recalc_rate,
	.determine_rate = clk_dummy_div_lcm_determine_rate,
	.set_rate = clk_dummy_div_set_rate,
};

struct clk_rate_change_divider_context {
	struct clk_dummy_context parent;
	struct clk_dummy_div child1, child2;
	struct clk *parent_clk, *child1_clk, *child2_clk;
};

struct clk_rate_change_divider_test_param {
	const char *desc;
	const struct clk_ops *ops;
	unsigned int extra_child_flags;
};

static const struct clk_rate_change_divider_test_param
clk_rate_change_divider_test_regular_ops_params[] = {
	{
		.desc = "regular_ops",
		.ops = &clk_dummy_div_ops,
		.extra_child_flags = 0,
	},
};

KUNIT_ARRAY_PARAM_DESC(clk_rate_change_divider_test_regular_ops,
		       clk_rate_change_divider_test_regular_ops_params, desc)

static const struct clk_rate_change_divider_test_param
clk_rate_change_divider_test_lcm_ops_v1_params[] = {
	{
		.desc = "lcm_ops_v1",
		.ops = &clk_dummy_div_lcm_ops,
		.extra_child_flags = 0,
	},
};

KUNIT_ARRAY_PARAM_DESC(clk_rate_change_divider_test_lcm_ops_v1,
		       clk_rate_change_divider_test_lcm_ops_v1_params, desc)

static const struct clk_rate_change_divider_test_param
clk_rate_change_divider_test_regular_ops_v2_params[] = {
	{
		.desc = "regular_ops_v2",
		.ops = &clk_dummy_div_ops,
		.extra_child_flags = CLK_V2_RATE_NEGOTIATION,
	},
};

KUNIT_ARRAY_PARAM_DESC(clk_rate_change_divider_test_regular_ops_v2,
		       clk_rate_change_divider_test_regular_ops_v2_params, desc)

static int clk_rate_change_divider_test_init(struct kunit *test)
{
	const struct clk_rate_change_divider_test_param *param = test->param_value;
	struct clk_rate_change_divider_context *ctx;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	test->priv = ctx;

	ctx->parent.hw.init = CLK_HW_INIT_NO_PARENT("parent", &clk_dummy_rate_ops, 0);
	ctx->parent.rate = 24 * HZ_PER_MHZ;
	ret = clk_hw_register_kunit(test, NULL, &ctx->parent.hw);
	if (ret)
		return ret;

	ctx->child1.hw.init = CLK_HW_INIT_HW("child1", &ctx->parent.hw,
					     param->ops,
					     CLK_SET_RATE_PARENT | param->extra_child_flags);
	ctx->child1.div = 1;
	ret = clk_hw_register_kunit(test, NULL, &ctx->child1.hw);
	if (ret)
		return ret;

	ctx->child2.hw.init = CLK_HW_INIT_HW("child2", &ctx->parent.hw,
					     param->ops,
					     CLK_SET_RATE_PARENT | param->extra_child_flags);
	ctx->child2.div = 1;
	ret = clk_hw_register_kunit(test, NULL, &ctx->child2.hw);
	if (ret)
		return ret;

	ctx->parent_clk = clk_hw_get_clk(&ctx->parent.hw, NULL);
	ret = clk_prepare_enable(ctx->parent_clk);
	if (ret)
		return ret;

	ctx->child1_clk = clk_hw_get_clk(&ctx->child1.hw, NULL);
	clk_prepare_enable(ctx->child1_clk);
	if (ret)
		return ret;

	ctx->child2_clk = clk_hw_get_clk(&ctx->child2.hw, NULL);
	clk_prepare_enable(ctx->child2_clk);
	if (ret)
		return ret;

	return 0;
}

static void clk_rate_change_divider_test_exit(struct kunit *test)
{
	struct clk_rate_change_divider_context *ctx = test->priv;

	clk_put(ctx->parent_clk);
	clk_put(ctx->child1_clk);
	clk_put(ctx->child2_clk);
}

/*
 * Test that, for a parent with two divider-only children with CLK_SET_RATE_PARENT set
 * and one requests a rate compatible with the existing parent rate, the parent and
 * sibling rates are not affected.
 */
static void clk_test_rate_change_divider_1(struct kunit *test)
{
	struct clk_rate_change_divider_context *ctx = test->priv;
	int ret;

	KUNIT_ASSERT_EQ(test, clk_get_rate(ctx->parent_clk), 24 * HZ_PER_MHZ);
	KUNIT_ASSERT_EQ(test, clk_get_rate(ctx->child1_clk), 24 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child1.div, 1);
	KUNIT_ASSERT_EQ(test, clk_get_rate(ctx->child2_clk), 24 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child2.div, 1);
	/* This test is expected to work with both v1 and v2 rate negotiation. */

	ret = clk_set_rate(ctx->child1_clk, 6 * HZ_PER_MHZ);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, clk_get_rate(ctx->parent_clk), 24 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, clk_get_rate(ctx->child1_clk), 6 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child1.div, 4);
	KUNIT_EXPECT_EQ(test, clk_get_rate(ctx->child2_clk), 24 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child2.div, 1);
}

static inline bool __clk_has_v2_negotiation(struct clk *clk)
{
	return clk_hw_get_flags(__clk_get_hw(clk)) & CLK_V2_RATE_NEGOTIATION;
}

/*
 * Test that, for a parent with two divider-only children with CLK_SET_RATE_PARENT
 * set and one requests a rate incompatible with the existing parent rate, the
 * sibling rate is also affected. This preserves existing behavior in the clk
 * core that some drivers may be unknowingly dependent on.
 */
static void clk_test_rate_change_divider_2_v1(struct kunit *test)
{
	struct clk_rate_change_divider_context *ctx = test->priv;
	int ret;

	KUNIT_ASSERT_EQ(test, clk_get_rate(ctx->parent_clk), 24 * HZ_PER_MHZ);
	KUNIT_ASSERT_EQ(test, clk_get_rate(ctx->child1_clk), 24 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child1.div, 1);
	KUNIT_ASSERT_EQ(test, clk_get_rate(ctx->child2_clk), 24 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child2.div, 1);
	KUNIT_ASSERT_TRUE(test, !__clk_has_v2_negotiation(ctx->child1_clk));
	KUNIT_ASSERT_TRUE(test, !__clk_has_v2_negotiation(ctx->child2_clk));

	ret = clk_set_rate(ctx->child1_clk, 32 * HZ_PER_MHZ);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/*
	 * The last sibling rate change is the one that was successful, and
	 * wins. The parent, and two children are all changed to 32 MHz. This
	 * keeps the long-standing behavior of the clk core that some drivers
	 * may be unknowingly dependent on.
	 */
	KUNIT_EXPECT_EQ(test, clk_get_rate(ctx->parent_clk), 32 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, clk_get_rate(ctx->child1_clk), 32 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child1.div, 1);
	KUNIT_EXPECT_EQ(test, clk_get_rate(ctx->child2_clk), 32 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child2.div, 1);
}

/*
 * Test that, for a parent with two divider-only children with CLK_SET_RATE_PARENT
 * set and one requests a rate incompatible with the existing parent rate, the
 * sibling rate is also affected. This preserves existing behavior in the clk
 * core that some drivers may be unknowingly dependent on. This test
 * demonstrates that even if the clk provider picks a parent rate that's
 * suitable for both children, the child's rate change also affects the
 * sibling's rate with the v1 rate negotiation logic.
 */
static void clk_test_rate_change_divider_3_v1(struct kunit *test)
{
	struct clk_rate_change_divider_context *ctx = test->priv;
	int ret;

	KUNIT_ASSERT_EQ(test, clk_get_rate(ctx->parent_clk), 24 * HZ_PER_MHZ);
	KUNIT_ASSERT_EQ(test, clk_get_rate(ctx->child1_clk), 24 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child1.div, 1);
	KUNIT_ASSERT_EQ(test, clk_get_rate(ctx->child2_clk), 24 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child2.div, 1);
	KUNIT_ASSERT_TRUE(test, !__clk_has_v2_negotiation(ctx->child1_clk));
	KUNIT_ASSERT_TRUE(test, !__clk_has_v2_negotiation(ctx->child2_clk));

	ret = clk_set_rate(ctx->child1_clk, 32 * HZ_PER_MHZ);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/*
	 * With LCM-based coordinated rate changes, the parent should be at
	 * 96 MHz (LCM of 32 and 24), child1 at 32 MHz, and child2 at 24 MHz.
	 * However, the clk core by default will clobber the sibling clk rate,
	 * so the sibling gets the parent rate of 96 MHz.
	 */
	KUNIT_EXPECT_EQ(test, clk_get_rate(ctx->parent_clk), 96 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, clk_get_rate(ctx->child1_clk), 32 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child1.div, 3);
	KUNIT_EXPECT_EQ(test, clk_get_rate(ctx->child2_clk), 96 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child2.div, 1);
}

/*
 * Test that, for a parent with two divider-only children with CLK_SET_RATE_PARENT
 * set and one requests a rate incompatible with the existing parent rate, the
 * sibling rate is not affected, and maintains it's rate when the v2 rate
 * negotiation logic is used.
 */
static void clk_test_rate_change_divider_4_v2(struct kunit *test)
{
	struct clk_rate_change_divider_context *ctx = test->priv;
	int ret;

	KUNIT_ASSERT_EQ(test, clk_get_rate(ctx->parent_clk), 24 * HZ_PER_MHZ);
	KUNIT_ASSERT_EQ(test, clk_get_rate(ctx->child1_clk), 24 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child1.div, 1);
	KUNIT_ASSERT_EQ(test, clk_get_rate(ctx->child2_clk), 24 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child2.div, 1);
	KUNIT_ASSERT_TRUE(test, __clk_has_v2_negotiation(ctx->child1_clk));
	KUNIT_ASSERT_TRUE(test, __clk_has_v2_negotiation(ctx->child2_clk));

	ret = clk_set_rate(ctx->child1_clk, 32 * HZ_PER_MHZ);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/*
	 * With LCM-based parent + v2 rate changes, the parent should be at
	 * 96 MHz (LCM of 32 and 24), child1 at 32 MHz, and child2 at 24 MHz.
	 */
	KUNIT_EXPECT_EQ(test, clk_get_rate(ctx->parent_clk), 96 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, clk_get_rate(ctx->child1_clk), 32 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child1.div, 3);
	KUNIT_EXPECT_EQ(test, clk_get_rate(ctx->child2_clk), 24 * HZ_PER_MHZ);
	KUNIT_EXPECT_EQ(test, ctx->child2.div, 4);
}

static struct kunit_case clk_rate_change_divider_cases[] = {
	KUNIT_CASE_PARAM(clk_test_rate_change_divider_1,
			 clk_rate_change_divider_test_regular_ops_gen_params),
	KUNIT_CASE_PARAM(clk_test_rate_change_divider_2_v1,
			 clk_rate_change_divider_test_regular_ops_gen_params),
	KUNIT_CASE_PARAM(clk_test_rate_change_divider_3_v1,
			 clk_rate_change_divider_test_lcm_ops_v1_gen_params),
	KUNIT_CASE_PARAM(clk_test_rate_change_divider_1,
			 clk_rate_change_divider_test_regular_ops_v2_gen_params),
	KUNIT_CASE_PARAM(clk_test_rate_change_divider_4_v2,
			 clk_rate_change_divider_test_regular_ops_v2_gen_params),
	{}
};

/*
 * Test suite that creates a parent with two divider-only children, and
 * documents the behavior of what happens to the sibling clock when one child
 * changes its rate.
 */
static struct kunit_suite clk_rate_change_divider_test_suite = {
	.name = "clk-rate-change-divider",
	.init = clk_rate_change_divider_test_init,
	.exit = clk_rate_change_divider_test_exit,
	.test_cases = clk_rate_change_divider_cases,
};

kunit_test_suites(
	&clk_rate_change_divider_test_suite,
);

MODULE_DESCRIPTION("Kunit tests for clk divider");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
MODULE_LICENSE("GPL");
