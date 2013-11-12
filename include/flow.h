﻿/*
	(C) Copyright Thierry Seegers 2010-2012. Distributed under the following license:

	Boost Software License - Version 1.0 - August 17th, 2003

	Permission is hereby granted, free of charge, to any person or organization
	obtaining a copy of the software and accompanying documentation covered by
	this license (the "Software") to use, reproduce, display, distribute,
	execute, and transmit the Software, and to prepare derivative works of the
	Software, and to permit third-parties to whom the Software is furnished to
	do so, all subject to the following:

	The copyright notices in the Software and this entire statement, including
	the above license grant, this restriction and the following disclaimer,
	must be included in all copies of the Software, in whole or in part, and
	all derivative works of the Software, unless such copies or derivative
	works are solely in the form of machine-executable object code generated by
	a source language processor.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
	SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
	FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
	ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
	DEALINGS IN THE SOFTWARE.
*/

#if !defined(FLOW_FLOW_H)
	 #define FLOW_FLOW_H

#include "graph.h"
#include "named.h"
#include "node.h"
#include "packet.h"
#include "pipe.h"
#include "timer.h"

#endif

/*!
\file flow.h

\brief Convenience header that includes all necessary headers.

\mainpage flow

\tableofcontents

\section introduction Introduction

flow is a headers-only <a href="http://en.wikipedia.org/wiki/C%2B%2B0x">C++11</a> 
framework which provides the building blocks for streaming data packets through a graph 
of data-transforming nodes. 
Note that this library has nothing to do with computer networking. 
In the context of this framework, a data packet is a slice of a data stream.
A \ref flow::graph will typically be composed of \ref flow::producer "producer nodes", \ref flow::transformer "transformer nodes" and 
\ref flow::consumer "consumer nodes".
A data packets is produced by a single producer node, can later go through any number of transformer nodes and is finally consumed by a single consumer node.
Nodes are connected to one another by \ref flow::pipe "pipes" attached to their \ref flow::inpin "input pins" and \ref flow::outpin "output pins".
The graph and base node classes already provide the necessary API to build a graph by connecting nodes together and to run the graph.
As a library user, you are only expected to write concrete node classes that perform the tasks you require.

Here's an example of a simple graph.
The two producers nodes could be capturing data from some hardware or be generating a steady stream of data on their own.
The transformer node processes the data coming in from both producers.
The transformer's output data finally goes to a consumer node.

\image html ./introduction_graph_simple.png "Data flow for a simple graph"

If we need to monitor the data coming in from <tt>producer 2</tt>, we can \ref flow::samples::generic::tee "tee" it to another consumer node.
This new consumer node could save all the data it receives to a file or log it in real-time without preserving it.
The \ref flow::samples::generic::tee "tee" transformer node is an example of a concrete node that duplicates incoming data to all its outputs.
It is provided in the framework and can be found in the \ref flow::samples::generic namespace.

\image html ./introduction_graph_tee.png "Data flow for a graph with a tee transformer node"

\section considerations Technical considerations

This implementation:
 - uses templates heavily.
 - depends on many of C++11's language features and library headers.
 - has been tested with Visual Studio 2012, GCC 4.6.3 and Xcode 4.5.2.
 - uses <a href="http://www.cmake.org">CMake</a> as the build and packaging tool. As a user of flow, you do not need to build anything it consists only in header files.
 - uses <a href="http://www.stack.nl/~dimitri/doxygen/index.html">Doxygen</a> to generate its documentation (and, optionally, <a href="http://www.graphviz.org/">Graphviz's dot</a>).

\section principles Design principles

\subsection use_unique_ptr Use of std::unique_ptr

When flowing through the graph, \ref flow::packet "data packets" are wrapped in std::unique_ptr. 
This helps memory managment tremendously and enforces the idea that, at any point in time, only a single entity -pipe or node- is responsible for a data packet.

\subsection thread_per_node A thread per node

flow is multi-threaded in that the \ref flow::graph "graph" assigns a thread of execution to each of its nodes.
The lifetime of these threads is taken care by \ref flow::graph "graph".
As a library user, the only mutli-threaded code you would write is whatever a node would require to perform its work.

\subsection node_state Node state

A node can be in one of three states: \ref flow::state::paused "paused", \ref flow::state::started "started" or \ref flow::state::stopped "stopped".
When instantiated, a node is in the \ref flow::state::paused "paused" state.
When a node is in the \ref flow::state::started "started" state, a thread of execution is created for it and it is actively consuming and/or producing packets.
When a node is in the \ref flow::state::paused "paused", it is no longer consuming and/or producing packets.
If a concrete node class has internal state, that state should be frozen such that, when the node is re-started, packet processing will continue as if the node had not been paused.
When a node is in the \ref flow::state::stopped "stopped" state, it's thread of execution is joined.
If a concrete node class has internal state, that state should be reset.

Before a node can transition to a new state, it must be added to a graph.
Transitioning between these states is done by calling a corresponding member function of the \ref flow::graph "graph" class.
For this relase, all nodes in a graph are always in the same state.
Regardless of the nodes' state, nodes can be added to and removed from a graph at any time and can be connected to and disconnected from another node at any time.

\subsection consumption_time Packet consumption time

Consumption time is the time at which a data packet can be set to be consumed by a consumer node.
When a data packet with an assigned consumption time arrives at a consumer node and the consumption time is:
 - in the future: the consumer node waits or sleeps until the current time and the consumption time match, then consumes the packet.
 - in the past: the packet is unused and discarded.

Node that consumption time is optional. 
Data packets with no consumption time are consumed as soon as they reach a consumer node.

\subsection named_things Named building blocks

All classes in flow, including the \ref flow::node "node" base class, derive from \ref flow::named "named".
That makes all concrete node classes required to be given a name too.
This feature serves two purposes:
 - nodes can be refered to by their names when building a graph, improving code readability greatly.
 - helps debugging, especially since all pins and pipes are also named and have names automatically generated based on what they are connected to.

\section samples Samples concrete nodes

As convenience, a small collection of concrete nodes is provided. 
They are found in the \ref flow::samples::generic and \ref flow::samples::math namespaces.

\section examples Examples

 - \subpage hello_world
 - \subpage multiplier

\section thanks Thanks

 - Volodymyr Frolov, author of the <a href="http://www.codeproject.com/KB/threads/lwsync.aspx">lwsync</a> library used in this project until version 3.0.
 - Dušan Rodina, maker of <a href="http://www.softwareideas.net/en/Default.aspx">Software Ideas Modeler</a>.
   Modeler was used to create the graph diagrams in the \ref introduction.

\section license License

\verbatim
Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
\endverbatim

*/

/*!

\page hello_world Hello, world!

In this example, we set up a graph with three \ref flow::samples::generic::generator "generators". 
Each of these generators operate on the same \ref flow::monotonous_timer "timer". 
Every time the timer fires, the generators produce a packet of data.
The data they produce is dictated by the functors given to them at construction time.
In this case, the functors are just functions that return strings.

The produced packets are then fed to an \ref flow::samples::math::adder "adder" transformer node. 
This adder uses operator+= internally. Thus, for strings, it concatenates its input.

Finally, the adder's output is connected to an \ref flow::samples::generic::ostreamer "ostreamer". 
This node simply streams the data packets it receives to a std::ostream of our choice, std::cout in this case.

\include hello_world.cpp
*/

/*!

\page multiplier Multiplication expression

In this example, we set up a graph with three \ref flow::samples::generic::generator "generators". 
Each of these generators operate on the same \ref flow::monotonous_timer "timer". 
Every time the timer fires, the generators produce a packet of data.
The data they produce is dictated by the functors given to them at construction time.
In this case, the functors are a reference to a random number generator.

The produced packets are then fed to a transformer defined locally.
This transformer takes its inputs (in terms of T), multiplies them using <tt>*=</tt>, then outputs the multiplication expression including the product as a string.
For example, given the inputs of 3 and 4, it outputs the string "3 * 4 = 12".

Finally, the transformer's output is connected to an \ref flow::samples::generic::ostreamer "ostreamer". 
This node simply streams the data packets it receives to a std::ostream of our choice, std::cout in this case.

\include multiplier.cpp

Here's the output of a run of about 30 seconds:

\code
4 * 4 * 0 = 0
10 * 3 * 9 = 270
9 * 5 * 8 = 360
2 * 7 * 0 = 0
8 * 7 * 10 = 560
1 * 5 * 5 = 25
8 * 0 * 10 = 0
6 * 3 * 2 = 36
1 * 7 * 3 = 21
3 * 10 * 0 = 0
\endcode
*/
