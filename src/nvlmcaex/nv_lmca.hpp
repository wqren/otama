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
#ifndef NV_VLAD_LMCA_HPP
#define NV_VLAD_LMCA_HPP

#include <string>
#include <sstream>
#include <queue>
#include "nv_vlad.hpp"
#include "nv_color_boc.h"
#include "nv_color_vlad.h"
#include "nv_color_hist.h"

typedef struct nv_lmca_result {
	float similarity;
	uint64_t index;
	
	inline bool
	operator>(const struct nv_lmca_result &lhs) const
	{
		return similarity > lhs.similarity;
	}
} nv_lmca_result_t;

typedef enum {
	NV_LMCA_FEATURE_VLAD,
	NV_LMCA_FEATURE_HSV,
	NV_LMCA_FEATURE_VLADHSV,
	NV_LMCA_FEATURE_VLAD_COLORCODE,
	NV_LMCA_FEATURE_VLAD_HSV
} nv_lmca_feature_e;
	

#define NV_LMCA_VLAD_DIM 128
#define NV_LMCA_HSV_DIM 64

typedef struct {
	float v[1];
} nv_lmca_empty_color_t;
typedef struct {
	NV_ALIGNED(float, v[4], 16);
} nv_lmca_colorcode_t;
typedef struct {
	NV_ALIGNED(float, v[NV_LMCA_HSV_DIM], 16);
} nv_lmca_hsv_t;

template<nv_lmca_feature_e T, typename C>
class nv_lmca_ctx
{
public:
	static const int LMCA_DIM = (T == NV_LMCA_FEATURE_HSV ? NV_LMCA_HSV_DIM : NV_LMCA_VLAD_DIM);
	static const int VLAD_DIM = nv_vlad_ctx<NV_VLAD_64>::DIM;
	static const int HSV_DIM = NV_COLOR_SHIST_DIM;
	static const int RAW_DIM = (T == NV_LMCA_FEATURE_HSV ? HSV_DIM : (T == NV_LMCA_FEATURE_VLADHSV ? VLAD_DIM + HSV_DIM : VLAD_DIM));
	static float VLAD_W(void) {
		return (T == NV_LMCA_FEATURE_VLADHSV ? sqrtf(0.7f) : 1.0f);
	};
	static float HSV_W(void) {
		return sqrtf(0.3f);
	}
	typedef struct {
		NV_ALIGNED(float, v[T == NV_LMCA_FEATURE_HSV ? NV_LMCA_HSV_DIM : NV_LMCA_VLAD_DIM], 32);
		NV_ALIGNED(C, color, 32);
	} vector_t;
	typedef enum {
		COLOR_METHOD_LINEAR,
		COLOR_METHOD_STEP
	} color_method_e;
	
private:
	nv_vlad_ctx<NV_VLAD_64> m_ctx;
	nv_matrix_t *m_lmca;
	nv_matrix_t *m_lmca2;
	
	void
	close(void)
	{
		nv_matrix_free(&m_lmca);
		nv_matrix_free(&m_lmca2);
	}

	static float
	distance(const float *v1,
			 const float *v2)
	{
		int i;
		NV_ALIGNED(float,  dist, 32);
		
#if NV_ENABLE_AVX
		NV_ASSERT(LMCA_DIM % 8 == 0);
		__m256 u = _mm256_setzero_ps();
		__m128 a;
		
		for (i = 0; i < LMCA_DIM; i += 8) {
			__m256 x = _mm256_load_ps(&v1[i]);
			__m256 h = _mm256_load_ps(&v2[i]);
			x = _mm256_sub_ps(x, h);
			x = _mm256_mul_ps(x, x);
			h = _mm256_hadd_ps(x, x);
			u = _mm256_add_ps(u, h);
		}
		u = _mm256_hadd_ps(u, u);
		a = _mm_add_ps(_mm256_extractf128_ps(u, 0), _mm256_extractf128_ps(u, 1));
		_mm_store_ss(&dist, a);
		
#elif NV_ENABLE_SSE
		NV_ASSERT(LMCA_DIM % 4 == 0);
		__m128 u = _mm_setzero_ps();
		NV_ALIGNED(float, mm[4], 16);
		
		for (i = 0; i < LMCA_DIM; i += 4) {
			__m128 x = _mm_sub_ps(_mm_load_ps(&v1[i]), *(const __m128 *)&v2[i]);
			u = _mm_add_ps(u, _mm_mul_ps(x, x));
		}
		_mm_store_ps(mm, u);
		dist = mm[0] + mm[1] + mm[2] + mm[3];
		
#else
		dist = 0.0f;
		for (i = 0; i < LMCA_DIM; ++i) {
			dist += (v1[i] - v2[i]) * (v1[i] - v2[i]);
		}
#endif
		return dist;
	}
	
public:	
	nv_lmca_ctx(): m_lmca(0), m_lmca2(0) {}
	~nv_lmca_ctx() { close(); }
	
	int
	set_vq_table(const char *file)
	{
		return m_ctx.set_vq_table(file);
	}
	int
	open(const char *lmca_file)
	{
		close();
		
		m_lmca = nv_load_matrix(lmca_file);
		if (m_lmca == NULL) {
			return -1;
		}
		if (m_lmca->n != RAW_DIM || m_lmca->m != LMCA_DIM) {
			nv_matrix_free(&m_lmca);
			return -1;
		}
		return 0;
	}
	
	int
	open(const char *lmca_file, const char *lmca2_file)
	{
		close();
			
		m_lmca = nv_load_matrix(lmca_file);
		if (m_lmca == NULL) {
			return -1;
		}
		m_lmca2 = nv_load_matrix(lmca2_file);
		if (m_lmca2 == NULL) {
			nv_matrix_free(&m_lmca);
			return -1;
		}
		if (m_lmca2->n != HSV_DIM || m_lmca2->m != NV_LMCA_HSV_DIM) {
			nv_matrix_free(&m_lmca);
			nv_matrix_free(&m_lmca2);
			return -1;
		}
		return 0;
	}
	
	void
	extract_vlad_vector(nv_matrix_t *vec, int vec_j, const nv_matrix_t *image)
	{
		m_ctx.extract(vec, vec_j, image);
		if (!(nv_vector_norm(vec, vec_j) > 0.0f)) {
			/* キーポイントがひとつもない場合は他と似ないように潰しておく */
			NV_MAT_V(vec, vec_j, 0) = -1.0f;
			nv_vector_normalize(vec, vec_j);
		}
		/* Power normalize */
		//m_ctx.power_normalize(vec, vec_j, 0.6f);
	}

	void
	extract_hsv_vector(nv_matrix_t *vec, int vec_j, const nv_matrix_t *image)
	{
		nv_color_shist(vec, vec_j, image);
	}
	void
	extract_raw_vector(nv_lmca_feature_e fv,
					   nv_matrix_t *vec, int vec_j, const nv_matrix_t *image)
	{
		if (fv == NV_LMCA_FEATURE_VLADHSV) {
			nv_matrix_t *vlad_vec = nv_matrix_alloc(VLAD_DIM, 1);
			nv_matrix_t *hsv_vec = nv_matrix_alloc(HSV_DIM, 1);
			
			extract_vlad_vector(vlad_vec, 0, image);
			extract_hsv_vector(hsv_vec, 0, image);

			nv_vector_muls(vlad_vec, 0, vlad_vec, 0, VLAD_W());
			nv_vector_muls(hsv_vec, 0, hsv_vec, 0, HSV_W());
			memcpy(&NV_MAT_V(vec, vec_j, 0), &NV_MAT_V(vlad_vec, 0, 0),
				   sizeof(float) * vlad_vec->n);
			memcpy(&NV_MAT_V(vec, vec_j, vlad_vec->n), &NV_MAT_V(hsv_vec, 0, 0),
				   sizeof(float) * hsv_vec->n);
			
			nv_matrix_free(&vlad_vec);
			nv_matrix_free(&hsv_vec);
		} else if (fv == NV_LMCA_FEATURE_HSV) {
			extract_hsv_vector(vec, vec_j, image);
		} else {
			extract_vlad_vector(vec, vec_j, image);
		}
	}
	
	void
	extract_color(nv_lmca_empty_color_t *color,
				  const nv_matrix_t *image, const float *colorcode)
	{}
	void
	extract_color(nv_lmca_colorcode_t *color,
				  const nv_matrix_t *image, const float *colorcode)
	{
		if (colorcode != NULL) {
			memcpy(color->v, colorcode, sizeof(color->v));
		} else {
			color->v[0] = -255.0f;
			color->v[1] = -255.0f;
			color->v[2] = -255.0f;
			color->v[3] = -255.0f;
		}
	}
	void
	extract_color(nv_lmca_hsv_t *color,
				  const nv_matrix_t *image, const float *colorcode)
	{
		NV_ASSERT(m_lmca2 != NULL);
		NV_ASSERT(m_lmca2->n == HSV_DIM);
		NV_ASSERT(m_lmca2->m == NV_LMCA_HSV_DIM);
		
		int i;
		float norm = 0.0f;
		nv_matrix_t *vec = nv_matrix_alloc(HSV_DIM, 1);
		
		extract_hsv_vector(vec, 0, image);
#ifdef _OPENMP
#pragma omp parallel for reduction (+:norm)
#endif
		for (i = 0; i < NV_LMCA_HSV_DIM; ++i) {
			float v = nv_vector_dot(m_lmca2, i, vec, 0);
			norm += v * v;
			color->v[i] = v;
		}
		if (norm > 0.0f) {
			float scale = 1.0f / sqrtf(norm);
			for (i = 0; i < NV_LMCA_HSV_DIM; ++i) {
				color->v[i] *= scale;
			}
		}
		nv_matrix_free(&vec);
	}
	
	void
	extract(vector_t *lmca, const nv_matrix_t *image,
			const float *colorcode = NULL)
	{
		int i;
		float norm = 0.0f;
		nv_matrix_t *vec = nv_matrix_alloc(RAW_DIM, 1);

		NV_ASSERT(m_lmca != NULL);
		NV_ASSERT(m_lmca->n == RAW_DIM);
		NV_ASSERT(m_lmca->m == LMCA_DIM);
		
		extract_raw_vector(T, vec, 0, image);
#ifdef _OPENMP
#pragma omp parallel for reduction (+:norm)
#endif
		for (i = 0; i < LMCA_DIM; ++i) {
			float v = nv_vector_dot(m_lmca, i, vec, 0);
			norm += v * v;
			lmca->v[i] = v;
		}
		/* ここは学習からするとnormalizeするべきではないが
		 * 学習時に想定外だったデータの悪影響が出にくいようにnormalizeする
		 */
		if (norm > 0.0f) {
			float scale = 1.0f / sqrtf(norm);
			for (i = 0; i < LMCA_DIM; ++i) {
				lmca->v[i] *= scale;
			}
		}
		extract_color(&lmca->color, image, colorcode);
		
		nv_matrix_free(&vec);
	}
	
	int
	extract(vector_t *vec, const void *data, size_t len,
			const float *colorcode = NULL)
	{
		nv_matrix_t *image = nv_decode_image(data, len);
		
		if (image == NULL) {
			return -1;
		}
		
		extract(vec, image, colorcode);
		nv_matrix_free(&image);
		
		return 0;
	}
	
	int
	extract(vector_t *vec, const char *file,
			const float *colorcode = NULL)
	{
		nv_matrix_t *image = nv_load_image(file);
		
		if (image == NULL) {
			return -1;
		}
		
		extract(vec, image, colorcode);
		nv_matrix_free(&image);
			
		return 0;
	}

	static void
	serialize(std::ostringstream &o, const nv_lmca_empty_color_t *color)
	{

	}
	static void
	serialize(std::ostringstream &o, const nv_lmca_colorcode_t *color)
	{
		int i;
		for (i = 0; i < 4; ++i) {
			o << std::scientific << color->v[i];
			o << " ";
		}
	}
	static void
	serialize(std::ostringstream &o, const nv_lmca_hsv_t *color)
	{
		int i;
		for (i = 0; i < NV_LMCA_HSV_DIM; ++i) {
			o << std::scientific << color->v[i];
			o << " ";
		}
	}
	static std::string
	serialize(const vector_t *vec)
	{
		int i;
		std::ostringstream o;
			
		for (i = 0; i < LMCA_DIM; ++i) {
			o << std::scientific << vec->v[i];
			o << " ";
		}
		serialize(o, &vec->color);
		
		return o.str();
	}
	
	static int
	deserialize(nv_lmca_empty_color_t *color, std::istringstream &in)
	{
		return 0;
	}
	static int
	deserialize(nv_lmca_colorcode_t *color, std::istringstream &in)
	{
		int i;
		for (i = 0; i < 4; ++i) {
			in >> std::skipws >> std::scientific >> color->v[i];
			if (in.fail()) {
				return -1;
			}
		}
		return 0;
	}
	static int
	deserialize(nv_lmca_hsv_t *color, std::istringstream &in)
	{
		int i;
		for (i = 0; i < NV_LMCA_HSV_DIM; ++i) {
			in >> std::skipws >> std::scientific >> color->v[i];
			if (in.fail()) {
				return -1;
			}
		}
		return 0;
	}
	
	static int
	deserialize(vector_t *vec, const char *s)
	{
		int i;
		std::istringstream in(s);

		for (i = 0; i < LMCA_DIM; ++i) {
			in >> std::skipws >> std::scientific >> vec->v[i];
			if (in.fail()) {
				return -1;
			}
		}
		return deserialize(&vec->color, in);
	}

	static inline float
	similarity(const nv_lmca_empty_color_t *v1,
			   const nv_lmca_empty_color_t *v2)
	{
		return 0.0f;
	}
	static inline float
	similarity(const nv_lmca_hsv_t *v1,
			   const nv_lmca_hsv_t *v2)
	{
		float dist = 0.0f;
		int i;
		for (i = 0; i < NV_LMCA_HSV_DIM; ++i) {
			float d = (v1->v[i] - v2->v[i]);
			dist += d * d;
		}
		return 1.0f - sqrtf(dist) * 0.5f;
	}
	static inline float
	similarity(const nv_lmca_colorcode_t *v1,
			   const nv_lmca_colorcode_t *v2)
	{
		const static float MAX_DIST = sqrtf(powf(255.0f, 2.0f) * 3.0f);
		float dist = (v1->v[0] - v2->v[0]) * (v1->v[0] - v2->v[0])
			+ (v1->v[1] - v2->v[1]) * (v1->v[1] - v2->v[1])
			+ (v1->v[2] - v2->v[2]) * (v1->v[2] - v2->v[2])
			+ (v1->v[3] - v2->v[3]) * (v1->v[2] - v2->v[3]);
		return (MAX_DIST - sqrtf(dist)) / MAX_DIST;
	}
	static inline float
	similarity(const float *v1,
			   const float *v2)
	{
		return 1.0f - sqrtf(distance(v1, v2)) * 0.5f;
	}
	
	static inline float
	similarity(const vector_t *v1,
			   const vector_t *v2,
			   color_method_e method,
			   const float color_weight,
			   const float color_threshold)
	{
		float lmca_similarity = similarity(v1->v, v2->v);
		float color_similarity = similarity(&v1->color, &v2->color);
		
		if (method == COLOR_METHOD_STEP) {
			if (color_threshold < color_similarity) {
				color_similarity = 1.0f;
			} else {
				color_similarity = 0.0f;
			}
		}
		return (1.0f - color_weight) * lmca_similarity + color_weight * color_similarity;
	}
	static int
	search(nv_lmca_result_t *results, int n,
		   const vector_t *db, int64_t ndb,
		   const vector_t *query,
		   color_method_e method,
		   const float color_weight,
		   const float color_threshold)
	{
		typedef std::priority_queue<nv_lmca_result_t, std::vector<nv_lmca_result_t>,
									std::greater<std::vector<nv_lmca_result_t>::value_type> > topn_t;
		int64_t i, imax;
		int threads = nv_omp_procs();
		topn_t topn;
		float *sim_min = nv_alloc_type(float, threads);
		std::vector<topn_t> topn_temp(threads);
		
#ifdef _OPENMP
#pragma omp parallel for num_threads(threads)
#endif
		for (i = 0; i < ndb; ++i) {
			int thread_idx = nv_omp_thread_id();
			nv_lmca_result_t new_node;
			
			if (color_weight == 1.0f) {
				new_node.similarity = similarity(&db[i].color, &query->color);
			} else if (color_weight == 0.0f) {
				new_node.similarity = similarity(db[i].v, query->v);
			} else {
				float lmca_similarity = similarity(db[i].v, query->v);
				float color_similarity = similarity(&db[i].color, &query->color);
				if (method == COLOR_METHOD_STEP) {
					if (color_threshold < color_similarity) {
						color_similarity = 1.0f;
					} else {
						color_similarity = 0.0f;
					}
				}
				new_node.similarity =
					(1.0f - color_weight) * lmca_similarity
					+ color_weight * color_similarity;
			}
			new_node.index = i;
			if (topn_temp[thread_idx].size() < (unsigned int)n) {
				topn_temp[thread_idx].push(new_node);
				sim_min[thread_idx] = topn_temp[thread_idx].top().similarity;
			} else if (new_node.similarity > sim_min[thread_idx]) {
				topn_temp[thread_idx].pop();
				topn_temp[thread_idx].push(new_node);
				sim_min[thread_idx] = topn_temp[thread_idx].top().similarity;
			}
		}
		for (i = 0; i < threads; ++i) {
			while (!topn_temp[i].empty()) {
				topn.push(topn_temp[i].top());
				topn_temp[i].pop();
			}
		}
		while (topn.size() > (unsigned int)n) {
			topn.pop();
		}
		imax = NV_MIN(n, ndb);
		for (i = imax - 1; i >= 0; --i) {
			results[i] = topn.top();
			topn.pop();
		}
		nv_free(sim_min);
		
		return (int)imax;
	}
	
	int
	make_train_data(nv_matrix_t **data, nv_matrix_t **labels,
					const char *filename, bool xflip = false)
	{
		char line[8192];
		FILE *fp;
		std::vector<std::string> list;
		int i, j, n;
		int ret = 0;
		
		fp = fopen(filename, "r");
		if (fp == NULL) {
			fprintf(stderr, "%s: %s\n", filename, strerror(errno));
			return -1;
		}
		while (fgets(line, sizeof(line) - 1, fp)) {
			size_t len = strlen(line);
			if (line[len-1] == '\n') {
				line[len-1] = '\0';
			}
			list.push_back(std::string(line));
		}
		fclose(fp);
		
		n = (int)list.size();
		
		*data = nv_matrix_alloc(RAW_DIM, n * (xflip ? 2 : 1));
		*labels = nv_matrix_alloc(1, n * (xflip ? 2 : 1));
		
		for (i = 0, j = 0; i < n; ++i) {
			const char *file = strstr(list[i].c_str(), " ");
			if (file) {
				nv_matrix_t *raw = nv_matrix_alloc((*data)->n, 1);
				nv_matrix_t *image = nv_load_image(file + 1);
				float lno = (float)atoi(list[i].c_str());
				
				if (image == NULL) {
					fprintf(stderr, "%s: error\n", filename);
					ret = -1;
					break;
				}
				extract_raw_vector(T, raw, 0, image);
				{
					nv_vector_copy(*data, j, raw, 0);
					NV_MAT_V(*labels, j, 0) = lno;
					++j;
				}
				if (xflip) {
					nv_matrix_t *flip = nv_matrix_clone(image);
					nv_flip_x(flip, image);

					extract_raw_vector(T, raw, 0, flip);
					{
						nv_vector_copy(*data, j, raw, 0);
						NV_MAT_V(*labels, j, 0) = lno;
						++j;
					}
					nv_matrix_free(&flip);
				}
				nv_matrix_free(&image);
			} else {
				fprintf(stderr, "%s: error\n", filename);
				ret = -1;
				break;
			}
		}
		if (ret != 0) {
			nv_matrix_free(data);
			nv_matrix_free(labels);
		}
		return ret;
	}
	
	static void
	train(nv_matrix_t *l, const nv_matrix_t *data, const nv_matrix_t *labels, int show,
		  int k, int k_n, float margin, float push_weight, float learning_rate, int max_iteration,
		  bool initialize = true
		)
	{
		printf("lmca_train: data: %d, labels: %d, k: %d, k_n: %d, margin: %f, push_weight: %f, learning_rate: %f, max_iteration: %d, resume: %s\n",
			   data->m, labels->m,
			   k, k_n, margin, push_weight, learning_rate, max_iteration,
			   initialize ? "false" : "true"
			);
		
		nv_lmca_progress(show);
		if (initialize) {
			nv_lmca_init_pca(l, data);
		}
		nv_lmca_train(l, data, labels, k, k_n, margin, push_weight, learning_rate, max_iteration);
		
		if (show) {
			int i;
			int cls = 0;
			nv_matrix_t *data_lmca = nv_matrix_alloc(LMCA_DIM, data->m);
			
			for (i = 0; i < labels->m; ++i) {
				cls = NV_MAX(cls, NV_MAT_VI(labels, i, 0));
			}
			cls += 1;
			nv_knn_result_t results[k];
			int ok;
			
			for (i = 0; i < data->m; ++i) {
				nv_lmca_projection(data_lmca, i, l, data, i);
			}
			ok = 0;
			for (i = 0; i < data->m; ++i) {
				int knn[cls];
				int j, n, max_v, max_i;
				
				memset(knn, 0, sizeof(knn));
				n = nv_knn(results, k, data_lmca, data_lmca, i);
				for (j = 0; j < n; ++j) {
					++knn[NV_MAT_VI(labels, results[j].index, 0)];
				}
				max_v = max_i= 0;
				for (j = 0; j < cls; ++j) {
					if (max_v < knn[j]) {
						max_v = knn[j];
						max_i = j;
					}
				}
				if (max_i == NV_MAT_VI(labels, i, 0)) {
					++ok;
				}
			}
			printf("Training Accuracy = %f%% (%d/%d)\n",
				   (float)ok / data->m * 100.0f,
				   ok, data->m);
			
			nv_matrix_free(&data_lmca);
		}
	}
};

class nv_lmca_vq
{
public:	
	static const int VQ_DIM = 32;
	static const int VQ_KMEANS_STEP = 100;
	static const int VQ_KEYPOINT_MAX = 3000;
	static float VQ_IMAGE_SIZE(void) {return 512.0f;}
	
private:

	static int
	read_filelist(const char *filename, std::vector<std::string> &filelist)
	{
		FILE *fp = fopen(filename, "r");
		char line[8192];
	
		if (fp == NULL) {
			fprintf(stderr, "%s: %s\n", filename, strerror(errno));
			return -1;
		}
		while (fgets(line, sizeof(line) - 1, fp)) {
			size_t len = strlen(line);
			const char *file;
			if (line[len-1] == '\n') {
				line[len-1] = '\0';
			}
			file = strstr(line, " ");
			if (file && strlen(file) > 0) {
				filelist.push_back(std::string(file + 1));
			} else {
				filelist.push_back(std::string(line));
			}
		}
		fclose(fp);
		
		return 0;
	}
	
	static int
	vq_extract(int sign,
			   nv_matrix_t *data,
			   const std::vector<std::string> &filelist,
			   bool xflip)
	{
		int j;
		int count = 0;
		int max_samples = data->m / filelist.size();
		nv_keypoint_param_t param = {
			16,
			NV_KEYPOINT_EDGE_THRESH,
			4.0f,
			NV_KEYPOINT_LEVEL,
			0.3f,
			NV_KEYPOINT_DETECTOR_STAR,
			NV_KEYPOINT_DESCRIPTOR_GRADIENT_HISTOGRAM
		};
		nv_keypoint_ctx_t *ctx = nv_keypoint_ctx_alloc(&param);

		if (xflip) {
			max_samples /= 2;
		}
		if (max_samples < 1) {
			max_samples = 1;
		}
	
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 1)
#endif
		for (j = 0; j < (int)filelist.size(); ++j) {
			int o;
			int flip_n = xflip ? 2:1;
			nv_matrix_t *image = nv_load(filelist[j].c_str());
			if (image == NULL) {
				printf("cant load %s\n", filelist[j].c_str());
				continue;
			}
			for (o = 0; o < flip_n; ++o) {
				int desc_m;
				nv_matrix_t *desc_vec = nv_matrix_alloc(NV_KEYPOINT_DESC_N, VQ_KEYPOINT_MAX);
				nv_matrix_t *key_vec = nv_matrix_alloc(NV_KEYPOINT_KEYPOINT_N, desc_vec->m);
				float scale = VQ_IMAGE_SIZE() / (float)NV_MAX(image->rows, image->cols);
				nv_matrix_t *gray = nv_matrix3d_alloc(1, image->rows, image->cols);
				nv_matrix_t *resize = nv_matrix3d_alloc(1, (int)(image->rows * scale),
													(int)(image->cols * scale));
				nv_matrix_t *smooth = nv_matrix3d_alloc(1, resize->rows, resize->cols);
				
				if (o == 1) {
					nv_matrix_t *flip = nv_matrix_clone(image);
					nv_flip_x(flip, image);
					nv_matrix_copy_all(image, flip);
					nv_matrix_free(&flip);
				}
				nv_gray(gray, image);
				nv_resize(resize, gray);
				nv_gaussian5x5(smooth, 0, resize, 0);
				
				desc_m = 0;
				nv_matrix_zero(desc_vec);
				
				desc_m = nv_keypoint_ex(ctx, key_vec, desc_vec, smooth, 0);
#ifdef _OPENMP
#pragma omp critical (nv_lmca_vq_extract)
#endif
				{
					int l;
					int *r_idx = (int *)nv_malloc(sizeof(int) * desc_m);
					int samples = NV_MIN(max_samples, desc_m);
					
					nv_shuffle_index(r_idx, 0, desc_m);
			
					for (l = 0; l < samples; ++l) {
						int i = r_idx[l];
						if (sign > 0) {
							if (NV_MAT_V(key_vec, i, NV_KEYPOINT_RESPONSE_IDX) > 0.0f) {
								nv_vector_copy(data, count, desc_vec, i);
								nv_vector_normalize(data, count);
								++count;
							}
						} else {
							if (NV_MAT_V(key_vec, i, NV_KEYPOINT_RESPONSE_IDX) < 0.0f) {
								nv_vector_copy(data, count, desc_vec, i);
								nv_vector_normalize(data, count);
								++count;
							}
						}
					}
					nv_free(r_idx);
				}
				nv_matrix_free(&desc_vec);
				nv_matrix_free(&key_vec);
				nv_matrix_free(&resize);
				nv_matrix_free(&gray);
				nv_matrix_free(&smooth);
			}
			nv_matrix_free(&image);
		}
		nv_keypoint_ctx_free(&ctx);
	
		return count;
	}

	static void
	vq_kmeans(nv_matrix_t *data,
			  nv_matrix_t *vq)
	{
		nv_matrix_t *labels = nv_matrix_alloc(1, data->m);
		nv_matrix_t *counts = nv_matrix_alloc(1, data->m);

		nv_matrix_zero(vq);
		nv_matrix_zero(labels);
		nv_matrix_zero(counts);

		//nv_kmeans_progress(1);
		nv_kmeans(vq, counts, labels, data, vq->m, VQ_KMEANS_STEP);
		
		nv_matrix_free(&labels);
		nv_matrix_free(&counts);
	}

public:
	static int
	train(int data_max, nv_matrix_t *vq_posi, nv_matrix_t *vq_nega,
		  const char *file, bool flip = false)
	{
		long t;
		nv_matrix_t *data;
		std::vector<std::string> filelist;
		int ret = 0;
		int m;
		
		if (read_filelist(file, filelist) != 0) {
			return -1;
		}
		printf("read filelist: %d\n", (int)filelist.size());
		
		data = nv_matrix_alloc(NV_KEYPOINT_DESC_N, data_max);
		nv_matrix_zero(vq_posi);
		nv_matrix_zero(vq_nega);
		nv_matrix_zero(data);
		
		t = nv_clock();
		m = vq_extract(1, data, filelist, flip);
		printf("posi keypoints: %d, %ldms\n", m, nv_clock() - t);
		t = nv_clock();
		nv_matrix_m(data, m);
		vq_kmeans(data, vq_posi);
		printf("posi keypoints clustring: %d, %ldms\n", m, nv_clock() - t);
		
		nv_matrix_m(data, data_max);
		nv_matrix_zero(data);
		
		t = nv_clock();
		m = vq_extract(-1, data, filelist, flip);
		printf("nega keypoints: %d, %ldms\n", m, nv_clock() - t);
		t = nv_clock();
		nv_matrix_m(data, m);
		vq_kmeans(data, vq_nega);
		printf("nega keypoints clustring: %d, %ldms\n", m, nv_clock() - t);
		
		nv_matrix_free(&data);
		
		return ret;
	}
};

#endif
