// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2025 BayLibre, SAS.
// Author: Valerio Setti <vsetti@baylibre.com>

#ifndef _MESON_AIU_AUDIN_FORMATTER_H
#define _MESON_AIU_AUDIN_FORMATTER_H

#include <linux/mutex.h>

struct regmap;
struct formatter;

struct stream_common_data {
	unsigned int fmt;
	unsigned int rate;
	unsigned int channels;
	unsigned int physical_width;

	struct mutex lock;
};

struct stream_data {
	struct formatter* formatter;
	struct stream_common_data *common;
	bool active;
};

struct formatter_ops {
	int (*enable)(struct formatter *formatter);
	int (*disable)(struct formatter *formatter);
	int (*prepare)(struct formatter *formatter,
		       struct stream_data *stream_data);
};

struct formatter {
	const struct formatter_ops *ops;
	struct regmap *regmap;
};

#endif /* _MESON_AIU_AUDIN_FORMATTER_H */
