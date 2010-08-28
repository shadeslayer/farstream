/*
 * Farsight2 - Farsight TFRC implementation
 *
 * Copyright 2010 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2010 Nokia Corp.
 *
 * tfrc.c - An implemention of TCP Friendly rate control, RFCs 5348 and 4828
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

#include "tfrc.h"

#include <math.h>
#include <string.h>

/*
 * ALL TIMES ARE IN MILLISECONDS
 */


/*
 * @s: segment size in bytes
 * @R: RTT in seconds
 * @p: loss event per packet transmitted
 *
 *                              s
 * X_Bps = -----------------------------------------------
 *         R * (sqrt(2*p/3) + 12*sqrt(3*p/8)*p*(1+32*p^2))
 *
 * Returns: The bitrate in bytes/s
 */
static gdouble
calculate_bitrate (gdouble s, gdouble R, gdouble p)
{
  gdouble f = sqrt (2*p/3) + 12*sqrt (3*p/8)*p*(1+32*pow (p, 2));

  return (1000 * s) / (R * f);
}

#define RECEIVE_RATE_HISTORY_SIZE      (4)

struct ReceiveRateItem {
  guint timestamp;
  guint rate;
};

struct _TfrcSender {
  guint computed_rate; /* The rate computer from the TCP throughput equation */

  gboolean sp;
  guint header_size;
  guint average_packet_size; /* 16 times larger */

  guint mss; /* max segment size */
  guint rate; /* maximum allowed sending rate in bytes/sec */
  guint inst_rate; /* corrected maximum allowed sending rate */
  guint averaged_rtt;
  guint sqmean_rtt;
  guint tld; /* Time Last Doubled during slow-start */

  guint initial_rate; /* Initial sending rate */

  guint segment_size; /* segment size */
  guint nofeedback_timer_expiry;

  guint retransmission_timeout; /* RTO */

  struct ReceiveRateItem receive_rate_history[RECEIVE_RATE_HISTORY_SIZE];

  guint last_loss_event_rate;

  gboolean sent_packet;
};

TfrcSender *
tfrc_sender_new (guint segment_size, guint now)
{
  TfrcSender *sender = g_slice_new0 (TfrcSender);

  /* initialized as described in RFC 5348 4.2 */
  sender->mss = 1460;
  sender->segment_size = segment_size;

  sender->rate = sender->segment_size;

  sender->nofeedback_timer_expiry = now + 2000; /* 2 seconds */

  return sender;
}

TfrcSender *
tfrc_sender_new_sp (guint now, guint initial_average_packet_size)
{
  TfrcSender *sender = tfrc_sender_new (1460, now);

  sender->sp = TRUE;
  /* Specified in RFC 4828 Section 3 second bullet */
  sender->header_size = 40;
  sender->average_packet_size = initial_average_packet_size;

  return sender;
}

void
tfrc_sender_free (TfrcSender *sender)
{
  g_slice_free (TfrcSender, sender);
}

void
tfrc_sender_on_first_rtt (TfrcSender *sender, guint now)
{
  sender->receive_rate_history[0].rate = G_MAXUINT;
  sender->receive_rate_history[0].timestamp = now;
}

static guint get_max_receive_rate (TfrcSender *sender, guint receive_rate,
  gboolean drop_max_uint)
{
  guint max_rate = receive_rate;
  guint i;

  for (i = 0; i < RECEIVE_RATE_HISTORY_SIZE; i++)
    if (G_UNLIKELY (drop_max_uint &&
            sender->receive_rate_history[i].rate == G_MAXUINT))
      sender->receive_rate_history[i].rate = 0;
    else
      max_rate = MAX (max_rate, sender->receive_rate_history[i].rate);

  return max_rate;
}

static guint
maximize_receive_rate_history (TfrcSender *sender, guint receive_rate,
    guint now)
{
  guint max_rate;

  max_rate = get_max_receive_rate (sender, receive_rate, TRUE);

  memset (sender->receive_rate_history, 0,
      sizeof(struct ReceiveRateItem) * RECEIVE_RATE_HISTORY_SIZE);

  sender->receive_rate_history[0].rate = max_rate;
  sender->receive_rate_history[0].timestamp = now;

  return max_rate;
}

static void
update_receive_rate_history (TfrcSender *sender, guint receive_rate, guint now)
{
  guint i;

  memmove (&sender->receive_rate_history[0],
      &sender->receive_rate_history[1],
      sizeof(sender->receive_rate_history[0])*(RECEIVE_RATE_HISTORY_SIZE - 1));

  sender->receive_rate_history[0].rate = receive_rate;
  sender->receive_rate_history[0].timestamp = now;

  for (i = 1; i < RECEIVE_RATE_HISTORY_SIZE; i++)
    if (sender->receive_rate_history[i].rate &&
        sender->receive_rate_history[i].timestamp <
        now - (2 * sender->averaged_rtt))
      sender->receive_rate_history[i].rate = 0;
}

const guint t_mbi = 64000; /* the maximum backoff interval of 64 seconds */

/* RFC 5348 section 4.3 step 4 second part */

static void
recompute_sending_rate (TfrcSender *sender, guint recv_limit,
    guint loss_event_rate, guint now)
{
  if (loss_event_rate > 0) {
    /* congestion avoidance phase */
    sender->computed_rate = calculate_bitrate (sender->segment_size,
        sender->averaged_rtt,
        loss_event_rate);
    sender->rate = MAX (MIN (sender->computed_rate, recv_limit),
        sender->segment_size/t_mbi);
  } else if (now - sender->tld >= sender->averaged_rtt) {
    /* initial slow-start */
    sender->rate = MAX ( MIN (2 * sender->rate, recv_limit),
        sender->initial_rate);
    sender->tld = now;
  }
}


void
tfrc_sender_on_feedback_packet (TfrcSender *sender, guint now,
    guint rtt, guint receive_rate, guint loss_event_rate,
    gboolean is_data_limited)
{
  guint recv_limit; /* the limit on the sending rate computed from X_recv_set */

  /* On first feedback packet, set he rate based on the mss and rtt */
  if (sender->tld == 0) {
    sender->initial_rate =
        (1000 * MIN (4*sender->mss, MAX (2*sender->mss, 4380))) / rtt;
    sender->rate = sender->initial_rate;
    sender->tld = now;
  }

  /* Apply the steps from RFC 5348 section 4.3 */

  /* Step 1 (calculating the rtt) is done before calling this function */

  /* Step 2: Update the RTT */
  if (sender->averaged_rtt == 0)
    sender->averaged_rtt = rtt;
  else
    sender->averaged_rtt = (sender->averaged_rtt *  9) / 10 + rtt / 10;

  /* Step 3: Update the timeout interval */
  sender->retransmission_timeout = MAX (4 * sender->averaged_rtt,
      1000 * 2 * sender->segment_size / sender->rate );

  /* Step 4: Update the allowed sending rate */


  if (G_UNLIKELY (is_data_limited)) {
    /* the entire interval covered by the feedback packet
       was a data-limited interval */
    if (loss_event_rate > sender->last_loss_event_rate) {
      guint i;
      /* the feedback packet reports a new loss event or an
         increase in the loss event rate p */

      /* Halve entries in the receive rate history */
      for (i = 0; i < RECEIVE_RATE_HISTORY_SIZE; i++)
        sender->receive_rate_history[i].rate /= 2;

      receive_rate *= 0.85;

      recv_limit = maximize_receive_rate_history (sender, receive_rate,
          now);
    } else {
      recv_limit =  2 * maximize_receive_rate_history (sender, receive_rate,
          now);
    }
  } else {
    /* typical behavior */
    update_receive_rate_history (sender, now, receive_rate);
    recv_limit = 2 * get_max_receive_rate (sender, 0, FALSE);
  }

  recompute_sending_rate (sender, recv_limit, loss_event_rate, now);

  /* Step 5: calculate the instantaneous
     transmit rate, X_inst, following Section 4.5.
  */

  if (sender->sqmean_rtt)
    sender->sqmean_rtt = 0.9 * sender->sqmean_rtt + sqrt(rtt) / 10;
  else
    sender->sqmean_rtt = sqrt(rtt);

  sender->inst_rate = sender->rate * sender->sqmean_rtt / sqrt(rtt);
  if (sender->inst_rate < sender->segment_size / t_mbi)
    sender->inst_rate = sender->segment_size / t_mbi;

  /* Step 6: Reset the nofeedback timer to expire after RTO seconds. */

  sender->nofeedback_timer_expiry = now + 2000;
  sender->sent_packet = FALSE;

  sender->last_loss_event_rate = loss_event_rate;
}

static void
update_limits(TfrcSender *sender, guint timer_limit, guint now)
{
  if (timer_limit < sender->segment_size / t_mbi)
    timer_limit = sender->segment_size / t_mbi;

  memset (sender->receive_rate_history, 0,
      sizeof(struct ReceiveRateItem) * RECEIVE_RATE_HISTORY_SIZE);

  sender->receive_rate_history[0].rate = timer_limit / 2;
  sender->receive_rate_history[0].timestamp = now;

  recompute_sending_rate (sender, timer_limit,
      sender->last_loss_event_rate, now);
}


void
tfrc_sender_no_feedback_timer_expired (TfrcSender *sender, guint now)
{
  guint receive_rate = get_max_receive_rate (sender, 0, FALSE);
  guint recover_rate = sender->initial_rate;

  if (sender->averaged_rtt == 0 /* && has not been idle ever since the nofeedback timer was set */) {
    /* We do not have X_Bps or recover_rate yet.
     * Halve the allowed sending rate.
     */

    sender->rate = MAX ( sender->rate / 2, sender->segment_size / t_mbi);
  } else if ((( sender->last_loss_event_rate > 0 &&
              receive_rate < recover_rate) ||
          (sender->last_loss_event_rate == 0 &&
              sender->rate < 2 * recover_rate)) &&
      sender->sent_packet) {
    /* Don't halve the allowed sending rate. */
    /* do nothing */
  } else if (sender->last_loss_event_rate == 0) {
    /* We do not have X_Bps yet.
     * Halve the allowed sending rate.
     */
    sender->rate = MAX ( sender->rate / 2, sender->segment_size / t_mbi);
  } else if (sender->computed_rate > 2 * receive_rate) {
    /* 2 * X_recv was already limiting the sending rate.
     * Halve the allowed sending rate.
   */
    update_limits(sender, receive_rate, now);
  } else {
    update_limits(sender, sender->computed_rate / 2, now);
  }

  sender->nofeedback_timer_expiry = now + MAX ( 4 * sender->averaged_rtt,
      2 * sender->segment_size / sender->rate);
}

void
tfrc_sender_sp_sending_packet (TfrcSender *sender, guint size)
{
  /* this should be:
   * avg = size + (avg * 15/16)
   */
  sender->average_packet_size =
      ( size << 4 ) + ((15 * sender->average_packet_size) >> 4);
}

guint
tfrc_sender_get_send_rate (TfrcSender *sender)
{
  if (sender->sp)
    return sender->rate * (sender->average_packet_size >> 4) /
        ((sender->average_packet_size >> 4) + sender->header_size);
  else
    return sender->rate;
}

guint
tfrc_sender_get_no_feedback_timer_expiry (TfrcSender *sender)
{
  return sender->nofeedback_timer_expiry;
}


#define NDUPACK 3 /* Number of packets to receive after a loss before declaring the loss event */
#define LOSS_EVENTS_MAX (8)
#define MAX_HISTORY_SIZE (LOSS_EVENTS_MAX * 2) /* 2 is a random number */

typedef struct  {
  guint first_timestamp;
  guint first_seqnum;
  guint first_recvtime;

  guint last_timestamp;
  guint last_seqnum;
  guint last_recvtime;
} ReceivedInterval;

struct _TfrcReceiver {
  GQueue received_intervals;

  gboolean sp;

  guint sender_rtt;
  guint receive_rate;
  guint feedback_timer_expiry;

  guint loss_event_rate;

  gboolean feedback_sent_on_last_timer;

  guint prev_received_bytes;
  guint prev_received_bytes_reset_time;
  guint received_bytes;
  guint received_bytes_reset_time;
};

TfrcReceiver *
tfrc_receiver_new (guint now)
{
  TfrcReceiver *receiver = g_slice_new0 (TfrcReceiver);

  g_queue_init (&receiver->received_intervals);
  receiver->received_bytes_reset_time = now;
  receiver->prev_received_bytes_reset_time = now;

  return receiver;
}


TfrcReceiver *
tfrc_receiver_new_sp (guint now)
{
  TfrcReceiver *receiver = tfrc_receiver_new (now);

  receiver->sp = TRUE;

  return receiver;
}

void
tfrc_receiver_free (TfrcReceiver *receiver)
{
  ReceivedInterval *ri;

  while ((ri = g_queue_pop_tail (&receiver->received_intervals)))
    g_slice_free (ReceivedInterval, ri);

  g_slice_free (TfrcReceiver, receiver);
}

/* Implements RFC 5348 section 5 */
static guint
calculate_loss_event_rate (TfrcReceiver *receiver, guint now)
{
  guint loss_event_times[LOSS_EVENTS_MAX];
  guint loss_event_seqnums[LOSS_EVENTS_MAX];
  guint loss_event_pktcount[LOSS_EVENTS_MAX];
  guint loss_intervals[LOSS_EVENTS_MAX + 1];
  const gdouble weights[8] = { 1.0, 1.0, 1.0, 1.0, 0.8, 0.6, 0.4, 0.2 };
  gint max_index = 0;
  guint received_count;
  guint lost_count;
  GList *item;
  guint loss_event_end = 0;
  guint max_seqnum = 0;
  guint i = 0;
  gdouble I_tot0 = 0;
  gdouble I_tot1 = 0;
  gdouble W_tot = 0;
  gdouble I_tot;

  if (receiver->sender_rtt == 0)
    return 0;

  for (item = g_queue_peek_tail_link (&receiver->received_intervals);
       item;
       item = item->prev) {
    ReceivedInterval *current = item->data;
    ReceivedInterval *prev = item->prev ? item->prev->data : NULL;

    if (!max_seqnum)
      max_seqnum = current->last_seqnum;

    /* Don't declare a loss event unless 3 packets have arrived after the loss
     */
    received_count += current->last_seqnum - current->first_seqnum;
    if (received_count < 3)
      continue;

    if (!prev)
      break;

    if (loss_event_end == 0)
      loss_event_end = current->first_timestamp;

    /* Add loss events that are entirely between prev and current */
     while (loss_event_end - receiver->sender_rtt > prev->last_timestamp) {
      loss_event_times[max_index] = loss_event_end - receiver->sender_rtt;
      loss_event_seqnums[max_index] = prev->last_seqnum +
          (current->first_timestamp - prev->last_seqnum) *
          ((loss_event_times[max_index] - prev->last_timestamp) /
              (prev->last_timestamp - current->first_timestamp));
      loss_event_end -= receiver->sender_rtt;
      /* First case is if the event ends before current */
      if (max_index &&
          loss_event_seqnums[max_index-1] < current->first_timestamp)
        loss_event_pktcount[max_index] = lost_count +
            (loss_event_seqnums[max_index-1] - loss_event_seqnums[max_index]);
      else
        loss_event_pktcount[max_index] = lost_count +
            (current->first_seqnum - loss_event_seqnums[max_index]);
      lost_count = 0;

      max_index++;
      if (max_index >= LOSS_EVENTS_MAX )
        goto done;
    }

    /* Add loss events that are between the start of prev and loss_event_end */
    if (loss_event_end - receiver->sender_rtt > prev->first_timestamp) {
      loss_event_seqnums[max_index] = prev->last_seqnum + 1;
      loss_event_times[max_index]  = prev->last_timestamp +
          ((current->first_timestamp - prev->last_timestamp) /
              (current->first_seqnum - prev->last_seqnum));
      loss_event_end = 0;
      loss_event_pktcount[max_index] = lost_count +
          (current->first_seqnum - loss_event_seqnums[max_index]);
      lost_count = 0;
      max_index++;

      if (max_index >= LOSS_EVENTS_MAX)
        goto done;
    }

    /* Count the number of lost packets not in an already counted loss event */
    if (loss_event_end == current->first_timestamp)
      lost_count += prev->last_seqnum - current->first_seqnum;
    else
      lost_count += prev->last_seqnum - loss_event_seqnums[max_index];
  }

 done:

  if (max_index == 0)
    return 0;

  /* RFC 5348 Section 5.3: The size of loss events */
  loss_intervals[0] = max_seqnum  - loss_event_seqnums[0] + 1;
  for (i = 1; i < max_index; i++)
    /* if its Small Packet variant and the loss event is short,
     * that is less than 2 * RTT, then the loss interval is divided by the
     * number of packets lost
     * see RFC 4828 section 3 bullet 3 paragraph 2 */
    if (receiver->sp &&
        loss_event_times[i-1] - loss_event_times[i] < 2 * receiver->sender_rtt)
      loss_intervals[i] = (loss_event_seqnums[i - 1] - loss_event_seqnums[i]) /
          loss_event_pktcount[i];
    else
      loss_intervals[i] = loss_event_seqnums[i - 1] - loss_event_seqnums[i];

  /* Section 5.4: Average loss rate */
  for (i = 0; i < max_index - 1; i++) {
    I_tot1 += loss_intervals[i] * weights[i-1];
    W_tot += weights[i];
  }

  /* Modified according to RFC 4828 section 3 bullet 3 paragraph 4*/
  if (receiver->sp && now - loss_event_times[0] < 2 * receiver->sender_rtt) {
    I_tot = I_tot1;
  } else {
    for (i = 1; i < max_index; i++)
      I_tot0 += loss_intervals[i] * weights[i];

    I_tot = MAX (I_tot0, I_tot1);
  }

  return W_tot / I_tot;
}


/* Implements RFC 5348 section 6.1 */
void
tfrc_receiver_got_packet (TfrcReceiver *receiver, guint timestamp,
    guint now, guint seqnum, guint sender_rtt, guint packet_size)
{
  GList *item = NULL;
  ReceivedInterval *current = NULL;
  ReceivedInterval *prev = NULL;
  gboolean recalculate_loss_rate = FALSE;

  receiver->received_bytes += packet_size;

  /* RFC 5348 section 6.3: First packet received */

  if (g_queue_get_length (&receiver->received_intervals) == 0 ||
      receiver->sender_rtt == 0) {
    if (receiver->sender_rtt)
      receiver->feedback_timer_expiry = now + receiver->sender_rtt;

    /* First packet, lets send a feedback packet */
    /* TODO: SEND FEEDBACK PACKET */
  }

  /* RFC 5348 section 6.1 Step 1: Add to packet history */

  for (item = g_queue_peek_tail_link (&receiver->received_intervals);
       item;
       item = item->prev) {
    current = item->data;
    prev = item->prev ? item->prev->data : NULL;

    if (G_LIKELY (seqnum == current->last_seqnum + 1)) {
      /* Extend the current packet forwardd */
      current->last_seqnum = seqnum;
      current->last_timestamp = timestamp;
      current->last_recvtime = now;
    } else if (seqnum >= current->first_seqnum ||
        seqnum <= current->last_seqnum) {
      /* Is inside the current interval, must be duplicate, ignore */
    } else if (seqnum > current->last_seqnum + 1) {
      prev = current;

      current = g_slice_new (ReceivedInterval);
      current->first_timestamp = current->last_timestamp = timestamp;
      current->first_seqnum = current->last_seqnum = seqnum;
      current->first_recvtime = current->last_recvtime = now;
      g_queue_push_tail (&receiver->received_intervals, current);

      item = g_queue_peek_tail_link (&receiver->received_intervals);
    } else if (seqnum == current->first_seqnum - 1) {
      /* Extend the current packet backwards */
      current->first_seqnum = seqnum;
      current->first_timestamp = timestamp;
      current->first_recvtime = now;
    } else if (seqnum < current->first_timestamp &&
        (!prev || seqnum > prev->last_seqnum + 1)) {
      /* We have something that goes in the middle of a gap,
         so lets created a new received interval */
      current = g_slice_new (ReceivedInterval);

      current->first_timestamp = current->last_timestamp = timestamp;
      current->first_seqnum = current->last_seqnum = seqnum;
      current->first_recvtime = current->last_recvtime = now;

      g_queue_insert_before (&receiver->received_intervals, item, current);
      item = item->prev;
      prev = item->prev ? item->prev->data : NULL;
    } else
      continue;
    break;
  }

  /* It's the first one or we're at the start */
  if (G_UNLIKELY (!current)) {
    /* If its before MAX_HISTORY_SIZE, its too old, just discard it */
    if (g_queue_get_length (&receiver->received_intervals) > MAX_HISTORY_SIZE)
      return;

    current = g_slice_new (ReceivedInterval);

    current->first_timestamp = current->last_timestamp = timestamp;
    current->first_seqnum = current->last_seqnum = seqnum;
    current->first_recvtime = current->last_recvtime = now;
    g_queue_push_head (&receiver->received_intervals, current);
  }

  if (g_queue_get_length (&receiver->received_intervals) > MAX_HISTORY_SIZE) {
    ReceivedInterval *remove = g_queue_pop_head (&receiver->received_intervals);

    if (remove == prev)
      prev = NULL;
    g_slice_free (ReceivedInterval, remove);
  }


  if (prev && (current->last_seqnum - current->first_seqnum == NDUPACK))
    recalculate_loss_rate = TRUE;


  if (prev &&  G_UNLIKELY (prev->last_seqnum + 1 == current->first_seqnum)) {
    /* Merge closed gap if any */
    current->first_seqnum = prev->first_seqnum;
    current->first_timestamp = prev->first_timestamp;
    current->first_recvtime = prev->first_recvtime;

    g_slice_free (ReceivedInterval, prev);
    g_queue_delete_link (&receiver->received_intervals, item->prev);

    prev = item->prev ? item->prev->data : NULL;

    recalculate_loss_rate = TRUE;
  }

  /* RFC 5348 section 6.1 Step 2, 3, 4:
   * Check if done
   * If not done, recalculte the loss event rate,
   * and possibly send a feedback message
   */

  if (recalculate_loss_rate || !receiver->feedback_sent_on_last_timer) {
    guint new_loss_event_rate = calculate_loss_event_rate (receiver, now);

    if (new_loss_event_rate > receiver->loss_event_rate ||
        !receiver->feedback_sent_on_last_timer)
      tfrc_receiver_feedback_timer_expired (receiver, now);
  }
}

gboolean
tfrc_receiver_feedback_timer_expired (TfrcReceiver *receiver, guint now)
{
  guint received_bytes = 0;
  guint received_bytes_reset_time = 0;

  if (receiver->received_bytes == 0) {
    receiver->feedback_timer_expiry = now + receiver->sender_rtt;
    receiver->feedback_sent_on_last_timer = FALSE;
    return FALSE;
  }

  receiver->loss_event_rate = calculate_loss_event_rate (receiver, now);

  if (now - receiver->received_bytes_reset_time > receiver->sender_rtt) {
    receiver->prev_received_bytes_reset_time =
        receiver->received_bytes_reset_time;
    receiver->prev_received_bytes = receiver->received_bytes;
    received_bytes = receiver->received_bytes;
    received_bytes_reset_time = receiver->received_bytes_reset_time;
  } else {
    receiver->prev_received_bytes += receiver->received_bytes;
    received_bytes = receiver->prev_received_bytes;
    received_bytes_reset_time = receiver->prev_received_bytes_reset_time;
  }
  receiver->received_bytes_reset_time = now;
  receiver->received_bytes = 0;

  receiver->receive_rate = received_bytes / received_bytes_reset_time;

  receiver->feedback_timer_expiry = now + receiver->sender_rtt;
  receiver->feedback_sent_on_last_timer = TRUE;

  return TRUE;
}


guint
tfrc_receiver_get_feedback_timer_expiry (TfrcReceiver *receiver)
{
  return receiver->feedback_timer_expiry;
}

guint
tfrc_receiver_get_receive_rate (TfrcReceiver *receiver)
{
  return receiver->receive_rate;
}

guint
tfrc_receiver_get_loss_event_rate (TfrcReceiver *receiver)
{
  return receiver->loss_event_rate;
}
