/* -*- c++ -*- */
/*
 * Copyright 2024 RigExpert.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_RIGEXPERT_FOBOS_SDR_H
#define INCLUDED_RIGEXPERT_FOBOS_SDR_H

#include <gnuradio/RigExpert/api.h>
#include <gnuradio/sync_block.h>

namespace gr 
{
    namespace RigExpert 
    {

        /*!
         * \brief <+description of block+>
         * \ingroup RigExpert
         *
         */
        class RIGEXPERT_API fobos_sdr : virtual public gr::sync_block
        {
        public:
            typedef std::shared_ptr<fobos_sdr> sptr;

            /*!
             * \brief Return a shared_ptr to a new instance of RigExpert::fobos_sdr.
             *
             * To avoid accidental use of raw pointers, RigExpert::fobos_sdr's
             * constructor is in a private implementation
             * class. RigExpert::fobos_sdr::make is the public interface for
             * creating new instances.
             */
            static sptr make(   int index = 0, 
                                double frequency = 100000000.0, 
                                double samplerate = 10000000.0,
                                int lna_gain = 0,
                                int vga_gain = 0,
                                int direct_sampling = 0,
                                int clock_source = 0);

            /**
             * @brief Callback for setting frequency
             */
            virtual void set_frequency(double freq) = 0;
            virtual void set_lna_gain(int lna_g) = 0;
            virtual void set_vga_gain(int vga_g) = 0;
        };

    } // namespace RigExpert
} // namespace gr

#endif /* INCLUDED_RIGEXPERT_FOBOS_SDR_H */
