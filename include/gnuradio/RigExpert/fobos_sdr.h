//==============================================================================
//       _____     __           _______
//      /  __  \  /_/          /  ____/                                __
//     /  /_ / / _   ____     / /__  __  __   ____    ____    ____   _/ /_
//    /    __ / / / /  _  \  / ___/  \ \/ /  / __ \  / __ \  / ___\ /  _/
//   /  /\ \   / / /  /_/ / / /___   /   /  / /_/ / /  ___/ / /     / /_
//  /_ /  \_\ /_/  \__   / /______/ /_/\_\ / ____/  \____/ /_/      \___/
//               /______/                 /_/             
//  Fobos SDR API library
//  Copyright (C) Rig Expert Ukraine Ltd.
//  2024.03.21
//  2024.04.08
//  2024.04.21
//  2024.04.26
//==============================================================================

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
                                double frequency_mhz = 100.0, 
                                double samplerate_mhz = 10.0,
                                int lna_gain = 0,
                                int vga_gain = 0,
                                int direct_sampling = 0,
                                int clock_source = 0);

            /**
             * @brief Callback for setting parameters on-the-fly
             */
            virtual void set_frequency(double frequency_mhz) = 0;
            virtual void set_samplerate(double samplerate_mhz) = 0;
            virtual void set_lna_gain(int lna_gain) = 0;
            virtual void set_vga_gain(int vga_gain) = 0;
            virtual void set_direct_sampling(int direct_sampling) = 0;
            virtual void set_clock_source(int clock_source) = 0;
        };

    } // namespace RigExpert
} // namespace gr

#endif /* INCLUDED_RIGEXPERT_FOBOS_SDR_H */
//==============================================================================