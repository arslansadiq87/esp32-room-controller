# Panel communication

The D1 network task owns the persistent WebSocket, command tracker and published controller state. The separate room-http task owns blocking HTTP operations for interactive configuration, read-back and history. A four-entry queue carries requests with a copied address, session and command. The network task consumes one completed result at a time and discards results from expired connections. Configuration POSTs check the current published session and command lifetime after their baseline GET. Provisioning tests the candidate address synchronously while the old socket is closed.

The panel sends explicit target states. A matching relay or dimmer event can confirm a pending request. Snapshot reconciliation covers lost events and already-satisfied states. Serial TRACE lines record queue time, send time, event/snapshot confirmation, and maximum service interval over each ten-second window.
