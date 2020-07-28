/*!
 * \file pulse_blanking_cc.cc
 * \brief Implements a pulse blanking algorithm
 * \author Javier Arribas (jarribas(at)cttc.es)
 *         Antonio Ramos  (antonio.ramosdet(at)gmail.com)
 * -----------------------------------------------------------------------------
 *
 * Copyright (C) 2010-2019 (see AUTHORS file for a list of contributors)
 *
 * GNSS-SDR is a software defined Global Navigation
 *          Satellite Systems receiver
 *
 * This file is part of GNSS-SDR.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * -----------------------------------------------------------------------------
 */

#include "pulse_blanking_cc.h"
#include <boost/math/distributions/chi_squared.hpp>
#include <gnuradio/io_signature.h>
#include <volk/volk.h>
#include <algorithm>


pulse_blanking_cc_sptr make_pulse_blanking_cc(float pfa, int32_t length_,
    int32_t n_segments_est, int32_t n_segments_reset)
{
    return pulse_blanking_cc_sptr(new pulse_blanking_cc(pfa, length_, n_segments_est, n_segments_reset));
}


pulse_blanking_cc::pulse_blanking_cc(float pfa,
    int32_t length_,
    int32_t n_segments_est,
    int32_t n_segments_reset) : gr::block("pulse_blanking_cc",
                                    gr::io_signature::make(1, 1, sizeof(gr_complex)),
                                    gr::io_signature::make(1, 1, sizeof(gr_complex)))
{
    const int32_t alignment_multiple = volk_get_alignment() / sizeof(gr_complex);
    set_alignment(std::max(1, alignment_multiple));
    this->pfa = pfa;
    this->length_ = length_;
    last_filtered = false;
    n_segments = 0;
    this->n_segments_est = n_segments_est;
    this->n_segments_reset = n_segments_reset;
    noise_power_estimation = 0.0;
    n_deg_fred = 2 * length_;
    boost::math::chi_squared_distribution<float> my_dist_(n_deg_fred);
    thres_ = boost::math::quantile(boost::math::complement(my_dist_, pfa));
    zeros_ = volk_gnsssdr::vector<gr_complex>(length_);
}


void pulse_blanking_cc::forecast(int noutput_items __attribute__((unused)), gr_vector_int &ninput_items_required)
{
    for (int &aux : ninput_items_required)
        {
            aux = length_;
        }
}


int pulse_blanking_cc::general_work(int noutput_items, gr_vector_int &ninput_items __attribute__((unused)),
    gr_vector_const_void_star &input_items, gr_vector_void_star &output_items)
{
    const auto *in = reinterpret_cast<const gr_complex *>(input_items[0]);
    auto *out = reinterpret_cast<gr_complex *>(output_items[0]);
    auto magnitude = volk_gnsssdr::vector<float>(noutput_items);
    volk_32fc_magnitude_squared_32f(magnitude.data(), in, noutput_items);
    int32_t sample_index = 0;
    float segment_energy;
    while ((sample_index + length_) < noutput_items)
        {
            volk_32f_accumulator_s32f(&segment_energy, (magnitude.data() + sample_index), length_);
            if ((n_segments < n_segments_est) && (last_filtered == false))
                {
                    noise_power_estimation = (static_cast<float>(n_segments) * noise_power_estimation + segment_energy / static_cast<float>(n_deg_fred)) / static_cast<float>(n_segments + 1);
                    memcpy(out, in, sizeof(gr_complex) * length_);
                }
            else
                {
                    if ((segment_energy / noise_power_estimation) > thres_)
                        {
                            memcpy(out, zeros_.data(), sizeof(gr_complex) * length_);
                            last_filtered = true;
                        }
                    else
                        {
                            memcpy(out, in, sizeof(gr_complex) * length_);
                            last_filtered = false;
                            if (n_segments > n_segments_reset)
                                {
                                    n_segments = 0;
                                }
                        }
                }
            in += length_;
            out += length_;
            sample_index += length_;
            n_segments++;
        }
    consume_each(sample_index);
    return sample_index;
}
