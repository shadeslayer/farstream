/*
 * Farsight2 - Farsight TFRC implementation
 *
 * Copyright 2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2010 Nokia Corp.
 *
 * tfrc.h - An implemention of TCP Friendly rate control, RFCs 5348 and 4828
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <glib.h>

#ifndef __TFRC_H__
#define __TFRC_H__

typedef struct _TfrcSender TfrcSender;
typedef struct _TfrcReceiver TfrcReceiver;

TfrcSender *tfrc_sender_new (guint segment_size, guint now);
TfrcSender *tfrc_sender_new_sp (guint now, guint initial_average_packet_size);
void tfrc_sender_free (TfrcSender *sender);

void tfrc_sender_on_first_rtt (TfrcSender *sender, guint now);
void tfrc_sender_on_feedback_packet (TfrcSender *sender, guint now, guint rtt,
    guint receive_rate, guint loss_event_rate, gboolean is_data_limited);
void tfrc_sender_no_feedback_timer_expired (TfrcSender *sender, guint now);

void tfrc_sender_sp_sending_packet (TfrcSender *sender, guint size);
guint tfrc_sender_get_send_rate (TfrcSender *sender);
guint tfrc_sender_get_no_feedback_timer_expiry (TfrcSender *sender);


TfrcReceiver *tfrc_receiver_new (void);
TfrcReceiver *tfrc_receiver_new_sp (void);
void tfrc_receiver_free (TfrcReceiver *receiver);

void tfrc_receiver_got_packet (TfrcReceiver *receiver, guint timestamp,
    guint now, guint seqnum, guint sender_rtt, guint packet_size);
gboolean tfrc_receiver_feedback_timer_expired (TfrcReceiver *receiver,
    guint now);
guint tfrc_receiver_get_feedback_timer_expiry (TfrcReceiver *receiver);

guint tfrc_receiver_get_receive_rate (TfrcReceiver *receiver);
guint tfrc_receiver_get_loss_event_rate (TfrcReceiver *receiver);


#endif /* __TFRC_H__ */
