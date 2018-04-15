#include <iostream>

#include "controller.hh"
#include "timestamp.hh"

using namespace std;

/* Default constructor */
Controller::Controller( const bool debug )
  :
  debug_( debug ),
  cur_window_size(10),
  packet_counter(0),
  //max_rtt(500),
  rtt_mean(0.0),
  rtt_std_dev(0.0),
  num_acks(0)
{}

/* Get current window size, in datagrams */
unsigned int Controller::window_size()
{
  /* Default: fixed window size of 100 outstanding datagrams */

  if ( debug_ ) {
    cerr << "At time " << timestamp_ms()
	 << " window size is " << cur_window_size << endl;
  }

  return cur_window_size;
}

/* A datagram was sent */
void Controller::datagram_was_sent( const uint64_t sequence_number,
				    /* of the sent datagram */
				    const uint64_t send_timestamp,
                                    /* in milliseconds */
				    const bool after_timeout
				    /* datagram was sent because of a timeout */ )
{
  /* Default: take no action */
  if (after_timeout) {
    cur_window_size /= 3;
  }
  if ( debug_ ) {
    cerr << "At time " << send_timestamp
	 << " sent datagram " << sequence_number << " (timeout = " << after_timeout << ")\n";
  }
}

/* An ack was received */
void Controller::ack_received( const uint64_t sequence_number_acked,
			       /* what sequence number was acknowledged */
			       const uint64_t send_timestamp_acked,
			       /* when the acknowledged datagram was sent (sender's clock) */
			       const uint64_t recv_timestamp_acked,
			       /* when the acknowledged datagram was received (receiver's clock)*/
			       const uint64_t timestamp_ack_received )
                               /* when the ack was received (by sender) */
{
  /* Default: take no action */
  const uint64_t cur_rtt = timestamp_ack_received - send_timestamp_acked;

  /* Compute running RTT mean and standard deviation */
  num_acks++;
  float prev_rtt_mean = rtt_mean;
  rtt_mean = rtt_mean + (cur_rtt - rtt_mean) / num_acks;
  rtt_std_dev = rtt_std_dev + (cur_rtt - rtt_mean) * (cur_rtt - prev_rtt_mean);

  /* Controlling number of outstanding packets. */
  if (packet_counter >= cur_window_size) {
    const uint64_t threshold = 130;
    if (cur_rtt >= threshold) {
      cur_window_size = cur_window_size * 2 / 3;
    } else {
      if (cur_rtt <= (rtt_mean + 2*rtt_std_dev)) {
        cur_window_size += 1;
      }
    }
    packet_counter = 0;
  } else {
    packet_counter += 1;
  }
  // max_rtt = max(max_rtt, cur_rtt);

  if ( debug_ ) {
    cerr << "At time " << timestamp_ack_received
	 << " received ack for datagram " << sequence_number_acked
	 << " (send @ time " << send_timestamp_acked
	 << ", received @ time " << recv_timestamp_acked << " by receiver's clock)"
	 << endl;
  }
}

/* How long to wait (in milliseconds) if there are no acks
   before sending one more datagram */
unsigned int Controller::timeout_ms()
{
  return 250; /* timeout of one second */
}
