// SPDX-License-Identifier: MIT
/*
 * The 'fsverity digest' command
 *
 * Copyright 2020 Microsoft
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include "fsverity.h"

#include <fcntl.h>
#include <getopt.h>

enum {
	OPT_HASH_ALG,
	OPT_BLOCK_SIZE,
	OPT_SALT,
	OPT_COMPACT,
	OPT_FOR_BUILTIN_SIG,
};

static const struct option longopts[] = {
	{"hash-alg",		required_argument, NULL, OPT_HASH_ALG},
	{"block-size",		required_argument, NULL, OPT_BLOCK_SIZE},
	{"salt",		required_argument, NULL, OPT_SALT},
	{"compact",		no_argument,	   NULL, OPT_COMPACT},
	{"for-builtin-sig",	no_argument,	   NULL, OPT_FOR_BUILTIN_SIG},
	{NULL, 0, NULL, 0}
};

struct fsverity_signed_digest {
	char magic[8];			/* must be "FSVerity" */
	__le16 digest_algorithm;
	__le16 digest_size;
	__u8 digest[];
};

/*
 * Compute the fs-verity measurement of the given file(s), for offline signing.
 */
int fsverity_cmd_digest(const struct fsverity_command *cmd,
		      int argc, char *argv[])
{
	u8 *salt = NULL;
	struct filedes file = { .fd = -1 };
	struct libfsverity_merkle_tree_params tree_params = { .version = 1 };
	bool compact = false, for_builtin_sig = false;
	int status;
	int c;

	while ((c = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
		switch (c) {
		case OPT_HASH_ALG:
			if (!parse_hash_alg_option(optarg,
						   &tree_params.hash_algorithm))
				goto out_usage;
			break;
		case OPT_BLOCK_SIZE:
			if (!parse_block_size_option(optarg,
						     &tree_params.block_size))
				goto out_usage;
			break;
		case OPT_SALT:
			if (!parse_salt_option(optarg, &salt,
					       &tree_params.salt_size))
				goto out_usage;
			tree_params.salt = salt;
			break;
		case OPT_COMPACT:
			compact = true;
			break;
		case OPT_FOR_BUILTIN_SIG:
			for_builtin_sig = true;
			break;
		default:
			goto out_usage;
		}
	}

	argv += optind;
	argc -= optind;

	if (argc < 1)
		goto out_usage;

	for (int i = 0; i < argc; i++) {
		struct fsverity_signed_digest *d = NULL;
		struct libfsverity_digest *digest = NULL;
		char digest_hex[FS_VERITY_MAX_DIGEST_SIZE * 2 +
				sizeof(struct fsverity_signed_digest) * 2 + 1];

		if (!open_file(&file, argv[i], O_RDONLY, 0))
			goto out_err;

		if (!get_file_size(&file, &tree_params.file_size))
			goto out_err;

		if (libfsverity_compute_digest(&file, read_callback,
					       &tree_params, &digest) != 0) {
			error_msg("failed to compute digest");
			goto out_err;
		}

		ASSERT(digest->digest_size <= FS_VERITY_MAX_DIGEST_SIZE);

		if (for_builtin_sig) {
			/*
			 * Format the digest for use with the built-in signature
			 * support.
			 */
			d = xzalloc(sizeof(*d) + digest->digest_size);
			memcpy(d->magic, "FSVerity", 8);
			d->digest_algorithm =
				cpu_to_le16(digest->digest_algorithm);
			d->digest_size = cpu_to_le16(digest->digest_size);
			memcpy(d->digest, digest->digest, digest->digest_size);

			bin2hex((const u8 *)d, sizeof(*d) + digest->digest_size,
				digest_hex);
		} else {
			bin2hex(digest->digest, digest->digest_size,
				digest_hex);
		}

		if (compact)
			printf("%s\n", digest_hex);
		else if (for_builtin_sig)
			printf("%s %s\n", digest_hex, argv[i]);
		else
			printf("%s:%s %s\n",
			       libfsverity_get_hash_name(digest->digest_algorithm),
			       digest_hex, argv[i]);

		filedes_close(&file);
		free(digest);
		free(d);
	}
	status = 0;
out:
	free(salt);
	return status;

out_err:
	filedes_close(&file);
	status = 1;
	goto out;

out_usage:
	usage(cmd, stderr);
	status = 2;
	goto out;
}
