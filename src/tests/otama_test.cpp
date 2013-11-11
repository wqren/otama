/*
 * This file is part of otama.
 *
 * Copyright (C) 2012 nagadomi@nurs.or.jp
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License,
 * or any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "otama.h"
#include "otama_util.h"
#include "otama_test.h"
#include <signal.h>

static void
handle_segv(int i)
{
	fprintf(stderr, "---- SEGV\n");
	NV_BACKTRACE;
	abort();
}

size_t
otama_test_read_file(const char *file, void **p, size_t *len)
{
	FILE *fp = fopen(file, "rb");
	size_t ret;
	
	fseek(fp, 0, SEEK_END);
	*len = ftell(fp);
	
	*p = nv_alloc_type(char, *len);
	rewind(fp);
	ret = fread(*p, sizeof(char), *len, fp);
	NV_ASSERT(ret == *len);
	fclose(fp);

	return ret;
}

static void
setup(void)
{
#if !OTAMA_MSVC
	nv_setenv("NV_BOVW_PKGDATADIR", NV_BOVW_PKGDATADIR);
#endif
	otama_mkdir("./data");
	otama_mkdir("./data/node1");
	otama_mkdir("./data/node2");
}

int main(void)
{
	signal(SIGSEGV, handle_segv);
	setup();

	otama_test_variant();
#if !OTAMA_MSVC
	otama_test_dbi();
	otama_test_vlad();
	otama_test_bovw();
#endif
	
	otama_test_api(OTAMA_TEST_CONFIG_DIR "/sim.yaml");
	otama_test_api(OTAMA_TEST_CONFIG_DIR "/id.yaml");
	otama_test_api(OTAMA_TEST_CONFIG_DIR "/color.yaml");
	otama_test_api(OTAMA_TEST_CONFIG_DIR "/bovw2k.yaml");
	otama_test_api(OTAMA_TEST_CONFIG_DIR "/bovw2k_sboc.yaml");	
	otama_test_api(OTAMA_TEST_CONFIG_DIR "/bovw8k.yaml");
	otama_test_api(OTAMA_TEST_CONFIG_DIR "/bovw512k_iv.yaml");
	otama_test_api(OTAMA_TEST_CONFIG_DIR "/sboc.yaml");
	otama_test_api(OTAMA_TEST_CONFIG_DIR "/lmca_vlad.yaml");
	otama_test_api(OTAMA_TEST_CONFIG_DIR "/lmca_hsv.yaml");
	otama_test_api(OTAMA_TEST_CONFIG_DIR "/lmca_vladhsv.yaml");
	otama_test_api(OTAMA_TEST_CONFIG_DIR "/lmca_vlad_hsv.yaml");
	otama_test_api(OTAMA_TEST_CONFIG_DIR "/lmca_vlad_colorcode.yaml");
	
#if OTAMA_WITH_KC
	otama_test_api(OTAMA_TEST_CONFIG_DIR "/bovw512k_iv_kc.yaml");
#endif
#if OTAMA_WITH_LEVELDB
	otama_test_api(OTAMA_TEST_CONFIG_DIR "/bovw512k_iv_ldb.yaml");
#endif
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/sim.yaml");
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/id.yaml");
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/color.yaml");
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/bovw2k.yaml");
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/bovw2k_sboc.yaml");	
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/bovw8k.yaml");
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/bovw512k_iv.yaml");
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/sboc.yaml");
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/vlad_nodb.yaml");
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/vlad128_nodb.yaml");
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/lmca_vlad.yaml");
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/lmca_hsv.yaml");
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/lmca_vladhsv.yaml");
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/lmca_vlad_hsv.yaml");
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/lmca_vlad_colorcode.yaml");

#if OTAMA_WITH_KC
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/bovw512k_iv_kc.yaml");
#endif
#if OTAMA_WITH_LEVELDB
	otama_test_similarity_api(OTAMA_TEST_CONFIG_DIR "/bovw512k_iv_ldb.yaml");
#endif	
#if !OTAMA_WINDOWS /* does not work on WindowsOS */
	otama_test_cluster(
		OTAMA_TEST_CONFIG_DIR "/bovw8k_node1.yaml",
		OTAMA_TEST_CONFIG_DIR "/bovw8k_node2.yaml");
#  if OTAMA_WITH_KC
	otama_test_cluster(
		OTAMA_TEST_CONFIG_DIR "/bovw512k_iv_kc_node1.yaml",
		OTAMA_TEST_CONFIG_DIR "/bovw512k_iv_kc_node2.yaml");
#  endif
#endif
	return 0;
}
