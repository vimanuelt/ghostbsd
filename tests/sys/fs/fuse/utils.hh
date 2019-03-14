/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 The FreeBSD Foundation
 *
 * This software was developed by BFF Storage Systems, LLC under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * TODO: remove FUSE_WRITE_CACHE definition when upgrading to protocol 7.9.
 * This bit was actually part of kernel protocol version 7.2, but never
 * documented until 7.9
 */
#ifndef FUSE_WRITE_CACHE
#define FUSE_WRITE_CACHE 1
#endif

class FuseTest : public ::testing::Test {
	protected:
	uint32_t m_maxreadahead;
	uint32_t m_init_flags;
	MockFS *m_mock = NULL;
	const static uint64_t FH = 0xdeadbeef1a7ebabe;

	public:
	int m_maxbcachebuf;

	/*
	 * libfuse's default max_readahead is UINT_MAX, though it can be
	 * lowered
	 */
	FuseTest(): FuseTest(UINT_MAX, 0) {}
	FuseTest(uint32_t maxreadahead): m_maxreadahead(maxreadahead) {}
	FuseTest(uint32_t maxreadahead, uint32_t init_flags):
		m_maxreadahead(maxreadahead), m_init_flags(init_flags) {}

	virtual void SetUp();

	virtual void TearDown() {
		if (m_mock)
			delete m_mock;
	}

	/*
	 * Create an expectation that FUSE_GETATTR will be called for the given
	 * inode any number of times.  It will respond with a few basic
	 * attributes, like the given size and the mode S_IFREG | 0644
	 */
	void expect_getattr(uint64_t ino, uint64_t size);

	/*
	 * Create an expectation that FUSE_LOOKUP will be called for the given
	 * path exactly times times.  It will respond with inode ino, mode
	 * mode, and cache validity forever.
	 */
	void expect_lookup(const char *relpath, uint64_t ino, mode_t mode,
		int times);

	/*
	 * Create an expectation that FUSE_GETATTR will be called for the given
	 * inode exactly times times.  It will return with open_flags flags and
	 * file handle FH.
	 */
	void expect_open(uint64_t ino, uint32_t flags, int times);

	/*
	 * Create an expectation that FUSE_OPENDIR will be called exactly once
	 * for inode ino.
	 */
	void expect_opendir(uint64_t ino);

	/*
	 * Create an expectation that FUSE_READ will be called exactly once for
	 * the given inode, at offset offset and with size isize.  It will
	 * return the first osize bytes from contents
	 */
	void expect_read(uint64_t ino, uint64_t offset, uint64_t isize,
		uint64_t osize, const void *contents);

	/* 
	 * Create an expectation that FUSE_RELEASE will be called times times
	 * for the given inode, returning error error
	 */
	void expect_release(uint64_t ino, int times, int error);

	/*
	 * Create an expectation that FUSE_WRITE will be called exactly once
	 * for the given inode, at offset offset, with write_flags flags, 
	 * size isize and buffer contents.  It will return osize
	 */
	void expect_write(uint64_t ino, uint64_t offset, uint64_t isize,
		uint64_t osize, uint32_t flags, const void *contents);
};
