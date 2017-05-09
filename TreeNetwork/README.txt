1) Sink sends out 'destroy' broadcast which disables the ability the send data and gets all nodes to reset their parents and forward on the destroy broadcast.
2) After 40 seconds the sink and all other nodes start sending out 'build' broadcasts with the nodes H value. This allows the nodes to build the a network of the number of hops it takes them to get a message to the sink.
3) After 45 seconds of 'build' broadcasts, the build phase finishes and sending data is enabled.
4) Clicking a button on a node will get it to send data along the network to the sink.
5) After 10 minutes, the sink will send out a destroy broadcast again, rebuilding the network.
