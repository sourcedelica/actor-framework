/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2016                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#ifndef CAF_IO_ABSTRACT_BROKER_HPP
#define CAF_IO_ABSTRACT_BROKER_HPP

#include <vector>
#include <unordered_map>

#include "caf/scheduled_actor.hpp"
#include "caf/prohibit_top_level_spawn_marker.hpp"
#include "caf/detail/intrusive_partitioned_list.hpp"

#include "caf/io/fwd.hpp"
#include "caf/io/accept_handle.hpp"
#include "caf/io/receive_policy.hpp"
#include "caf/io/endpoint_handle.hpp"
#include "caf/io/system_messages.hpp"
#include "caf/io/connection_handle.hpp"
#include "caf/io/datagram_sink_handle.hpp"
#include "caf/io/datagram_source_handle.hpp"

#include "caf/io/network/native_socket.hpp"
#include "caf/io/network/stream_manager.hpp"
#include "caf/io/network/acceptor_manager.hpp"
#include "caf/io/network/endpoint_manager.hpp"
#include "caf/io/network/datagram_sink_manager.hpp"
#include "caf/io/network/datagram_source_manager.hpp"

namespace caf {
namespace io {

class middleman;

/// @defgroup Broker Actor-based Network Abstraction
///
/// Brokers provide an actor-based abstraction for low-level network IO.
/// The central component in the network abstraction of CAF is the
/// `middleman`. It connects any number of brokers to a `multiplexer`,
/// which implements a low-level IO event loop.
///
/// ![Relation between middleman, multiplexer, and broker](broker.png)
///
/// Brokers do *not* operate on sockets or other platform-dependent
/// communication primitives. Instead, brokers use a `connection_handle`
/// to identify a reliable, end-to-end byte stream (e.g. a TCP connection)
/// and `accept_handle` to identify a communication endpoint others can
/// connect to via its port.
///
/// Each `connection_handle` is associated with a `scribe` that provides
/// access to an output buffer as well as a `flush` operation to request
/// sending its content via the network. Instead of actively receiving data,
/// brokers configure a scribe to asynchronously receive data, e.g.,
/// `self->configure_read(hdl, receive_policy::exactly(1024))` would
/// configure the scribe associated to `hdl` to receive *exactly* 1024 bytes
/// and generate a `new_data_msg` message for the broker once the
/// data is available. The buffer in this message will be re-used by the
/// scribe to minimize memory usage and heap allocations.
///
/// Each `accept_handle` is associated with a `doorman` that will create
/// a `new_connection_msg` whenever a new connection was established.
///
/// All `scribe` and `doorman` instances are managed by the `multiplexer`

/// A broker mediates between actor systems and other components in the network.
/// @ingroup Broker
class abstract_broker : public scheduled_actor,
                        public prohibit_top_level_spawn_marker {
public:
  virtual ~abstract_broker();

  // even brokers need friends
  friend class scribe;
  friend class doorman;
  friend class endpoint;
  friend class datagram_sink;
  friend class datagram_source;

  // -- overridden modifiers of abstract_actor ---------------------------------

  void enqueue(mailbox_element_ptr, execution_unit*) override;

  void enqueue(strong_actor_ptr, message_id, message, execution_unit*) override;

  // -- overridden modifiers of local_actor ------------------------------------

  void launch(execution_unit* eu, bool lazy, bool hide) override;

  // -- overridden modifiers of abstract_broker --------------------------------

  bool cleanup(error&& reason, execution_unit* host) override;

  // -- overridden modifiers of resumable --------------------------------------

  resume_result resume(execution_unit*, size_t) override;

  // -- modifiers --------------------------------------------------------------

  /// Suspends activities on `hdl` unconditionally.
  template <class Handle>
  void halt(Handle hdl) {
    auto ref = by_id(hdl);
    if (ref)
      ref->halt();
  }

  /// Allows activities on `hdl` unconditionally (default).
  template <class Handle>
  void trigger(Handle hdl) {
    auto ref = by_id(hdl);
    if (ref)
      ref->trigger();
  }

  /// Allows `num_events` activities on `hdl`.
  template <class Handle>
  void trigger(Handle hdl, size_t num_events) {
    auto ref = by_id(hdl);
    if (!ref)
      return;
    if (num_events > 0) {
      ref->trigger(num_events);
    } else {
      // if we have any number of activity tokens, ignore this call
      // otherwise (currently in unconditional receive state) halt
      auto x = ref->activity_tokens();
      if (!x)
        ref->halt();
    }
  }

  /// Modifies the receive policy for given connection.
  /// @param hdl Identifies the affected connection.
  /// @param config Contains the new receive policy.
  void configure_read(connection_handle hdl, receive_policy::config config);

  /// Enables or disables write notifications for given connection.
  void ack_writes(connection_handle hdl, bool enable);

  /// Returns the write buffer for given connection.
  std::vector<char>& wr_buf(connection_handle hdl);

  /// Writes `data` into the buffer for given connection.
  void write(connection_handle hdl, size_t data_size, const void* data);

  /// Sends the content of the buffer for given connection.
  void flush(connection_handle hdl);

  /// Enables or disables write notifications for given datagram socket.
  void ack_writes(datagram_sink_handle hdl, bool enable);

  /// Modifies the buffer for received datagrams.
  /// @param hdl Identifies the affected socket.
  /// @param buf_size Size of the receiver buffer for the next datagram.
  void configure_datagram_size(datagram_source_handle hdl, size_t buf_size);

  /// Returns write buffer for given sink.
  std::vector<char>& wr_buf(datagram_sink_handle hdl);

  /// Writes `data` into the buffer of a given sink.
  void write(datagram_sink_handle hdl, size_t data_size, const void* data);

  /// Returns the middleman instance this broker belongs to.
  inline middleman& parent() {
    return system().middleman();
  }

  /// Adds a `scribe` instance to this broker.
  void add_scribe(const intrusive_ptr<scribe>& ptr);

  /// Tries to connect to `host` on given `port` and creates
  /// a new scribe describing the connection afterwards.
  /// @returns The handle of the new `scribe` on success.
  expected<connection_handle> add_tcp_scribe(const std::string& host,
                                             uint16_t port);

  /// Assigns a detached `scribe` instance identified by `hdl`
  /// from the `multiplexer` to this broker.
  expected<void> assign_tcp_scribe(connection_handle hdl);

  /// Creates and assigns a new `scribe` from given native socked `fd`.
  expected<connection_handle> add_tcp_scribe(network::native_socket fd);

  /// Adds a `doorman` instance to this broker.
  void add_doorman(const intrusive_ptr<doorman>& ptr);

  /// Tries to open a local port and creates a `doorman` managing
  /// it on success. If `port == 0`, then the broker will ask
  /// the operating system to pick a random port.
  /// @returns The handle of the new `doorman` and the assigned port.
  expected<std::pair<accept_handle, uint16_t>>
  add_tcp_doorman(uint16_t port = 0, const char* in = nullptr,
                  bool reuse_addr = false);

  /// Assigns a detached `doorman` instance identified by `hdl`
  /// from the `multiplexer` to this broker.
  expected<void> assign_tcp_doorman(accept_handle hdl);

  /// Creates and assigns a new `doorman` from given native socked `fd`.
  expected<accept_handle> add_tcp_doorman(network::native_socket fd);

  /// Adds a `datagram_sink` instance to this broker.
  void add_datagram_sink(const intrusive_ptr<datagram_sink>& ptr);

  /// Tries to create a datgram sink for `host` on given `port` and creates
  /// a new datagram sink describing the endpoint afterwards.
  /// @returns The handle of the new `datagram_sink` on success.
  expected<datagram_sink_handle> add_datagram_sink(const std::string& host,
                                                   uint16_t port);

  /// Assigns a detached `datagram_sink` instance identified by `hdl`
  /// from the `multiplexer` to this broker.
  expected<void> assign_datagram_sink(datagram_sink_handle hdl);

  /// Creates and assigns a new `datagram_sink` from given native socked `fd`.
  expected<datagram_sink_handle> add_datagram_sink(network::native_socket fd);

  /// Adds a `datagram_source` instance to this broker.
  void add_datagram_source(const intrusive_ptr<datagram_source>& ptr);

  /// Tries to open a local port and creates a `datagram_source` managing
  /// it on success. If `port == 0`, then the broker will ask
  /// the operating system to pick a random port.
  /// @returns The handle of the new `datagram_source` and the assigned port.
  expected<std::pair<datagram_source_handle, uint16_t>>
  add_datagram_source(uint16_t port = 0, const char* in = nullptr,
                      bool reuse_addr = false);

  /// Assigns a detached `datagram_source` instance identified by `hdl`
  /// from the `multiplexer` to this broker.
  expected<void> assign_datagram_source(datagram_source_handle hdl);

  /// Creates and assigns a new `datagram_source` from given native socked `fd`.
  expected<datagram_source_handle>
  add_datagram_source(network::native_socket fd);

  /// Returns the remote address associated to `hdl`
  /// or empty string if `hdl` is invalid.
  std::string remote_addr(connection_handle hdl);

  /// Returns the remote port associated to `hdl`
  /// or `0` if `hdl` is invalid.
  uint16_t remote_port(connection_handle hdl);

  /// Returns the local address associated to `hdl`
  /// or empty string if `hdl` is invalid.
  std::string local_addr(accept_handle hdl);

  /// Returns the local port associated to `hdl` or `0` if `hdl` is invalid.
  uint16_t local_port(accept_handle hdl);

  /// Returns the handle associated to given local `port` or `none`.
  accept_handle hdl_by_port(uint16_t port);

  /// Returns the remote address associated to `hdl`
  /// or empty string if `hdl` is invalid.
  std::string remote_addr(datagram_sink_handle hdl);

  /// Returns the remote port associated to `hdl`
  /// or `0` if `hdl` is invalid.
  uint16_t remote_port(datagram_sink_handle hdl);

  /// Returns the local port associated to `hdl` or `0` if `hdl` is invalid.
  uint16_t local_port(datagram_source_handle hdl);

  /// Closes all connections and acceptors.
  void close_all();

  /// Closes the connection or acceptor identified by `handle`.
  /// Unwritten data will still be send.
  template <class Handle>
  bool close(Handle hdl) {
    auto x = by_id(hdl);
    if (!x)
      return false;
    x->stop_reading();
    return true;
  }

  /// Checks whether `hdl` is assigned to broker.
  template <class Handle>
  bool valid(Handle hdl) {
    return get_map(hdl).count(hdl) > 0;
  }

  /// @cond PRIVATE
  template <class Handle>
  void erase(Handle hdl) {
    auto& elements = get_map(hdl);
    auto i = elements.find(hdl);
    if (i != elements.end())
      elements.erase(i);
  }
  /// @endcond

  // -- overridden observers of abstract_actor ---------------------------------

  const char* name() const override;

  // -- overridden observers of resumable --------------------------------------

  subtype_t subtype() const override;

  // -- observers --------------------------------------------------------------

  /// Returns the number of open connections.
  inline size_t num_connections() const {
    return scribes_.size();
  }

  /// Returns all handles of all `scribe` instances attached to this broker.
  std::vector<connection_handle> connections() const;

protected:
  void init_broker();

  explicit abstract_broker(actor_config& cfg);

  using doorman_map = std::unordered_map<accept_handle, intrusive_ptr<doorman>>;

  using scribe_map = std::unordered_map<connection_handle,
                                        intrusive_ptr<scribe>>;

  using datagram_sink_map = std::unordered_map<datagram_sink_handle,
                                               intrusive_ptr<datagram_sink>>;

  using datagram_source_map = std::unordered_map<datagram_source_handle,
                                                 intrusive_ptr<datagram_source>>;

  using endpoint_map = std::unordered_map<endpoint_handle,
                                          intrusive_ptr<endpoint>>;

  /// @cond PRIVATE

  // meta programming utility
  inline doorman_map& get_map(accept_handle) {
    return doormen_;
  }

  // meta programming utility
  inline scribe_map& get_map(connection_handle) {
    return scribes_;
  }

  // meta programming utility
  inline datagram_sink_map& get_map(datagram_sink_handle) {
    return datagram_sinks_;
  }

  // meta programming utility
  inline datagram_source_map& get_map(datagram_source_handle) {
    return datagram_sources_;
  }
  // meta programming utility (not implemented)
  static intrusive_ptr<doorman> ptr_of(accept_handle);

  // meta programming utility (not implemented)
  static intrusive_ptr<scribe> ptr_of(connection_handle);

  // meta programming utility (not implemented)
  static intrusive_ptr<datagram_sink> ptr_of(datagram_sink_handle);

  // meta programming utility (not implemented)
  static intrusive_ptr<datagram_source> ptr_of(datagram_source_handle);

  /// @endcond

  /// Returns the `multiplexer` running this broker.
  network::multiplexer& backend();

  /// Returns a `scribe`, `doorman`, `datagram_sink` or `datagram_source`
  /// identified by `hdl`.
  template <class Handle>
  auto by_id(Handle hdl) -> optional<decltype(*ptr_of(hdl))> {
    auto& elements = get_map(hdl);
    auto i = elements.find(hdl);
    if (i == elements.end())
      return none;
    return *(i->second);
  }

  /// Returns an intrusive pointer to a `scribe`, `doorman`, `datagram_sink`
  /// or `datagram_source` identified by `hdl` and remove it from this broker.
  template <class Handle>
  auto take(Handle hdl) -> decltype(ptr_of(hdl)) {
    using std::swap;
    auto& elements = get_map(hdl);
    decltype(ptr_of(hdl)) result;
    auto i = elements.find(hdl);
    if (i == elements.end())
      return nullptr;
    swap(result, i->second);
    elements.erase(i);
    return result;
  }

private:
  scribe_map scribes_;
  doorman_map doormen_;
  datagram_sink_map datagram_sinks_;
  datagram_source_map datagram_sources_;
  detail::intrusive_partitioned_list<mailbox_element, detail::disposer> cache_;
  std::vector<char> dummy_wr_buf_;
};

} // namespace io
} // namespace caf

#endif // CAF_IO_ABSTRACT_BROKER_HPP
