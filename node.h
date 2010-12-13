#if !defined(FLOW_NODE_H)
	 #define FLOW_NODE_H

#include "named.h"
#include "packet.h"
#include "pipe.h"

#include <lwsync/critical_resource.hpp>
#include <lwsync/monitor.hpp>

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

//!\namespace flow
//!
//!\brief All basic blocks to build a graph of packets streaming across nodes.
namespace flow
{

class inpin;
class node;
class outpin;

//!\enum flow::state
//!
//! The state of a node.
//! State is never set directly.
//! Transitions from a state to another is requested from the node.
enum state
{
	start_requested,	//!< The node has been requested to transition to the started state.
	started,			//!< The node is in the started state.
	incoming,			//!< This state is used by a producing node to indicate to its connected consuming node that a packet was put in the pipe.
	pause_requested,	//!< The node has been requested to transition to the paused state.
	paused,				//!< The node is in the paused state.
	stop_requested,		//!< The node has been requested to stop execution and return from its thread.
};

//!\brief Base class for a node's inlet or outlet.
//!
//! Pins are connected to one another through pipes.
//! Even when one pin is disconnected, the pipe remains attached to the remaining pin to mimize packet loss.
class pin : public named
{
protected:
	std::shared_ptr<lwsync::critical_resource<pipe> > d_pipe_cr_sp; //!< Shared ownership of a pipe with the pin to which this pin is connected.

public:
	//!\brief The flow direction of this pin.
	enum dir
	{
		in,		//!< Flows into the node.
		out		//!< Flows out of the node.
	};

	//!\param name_r The name of this pin. This will be typically generated from the name of the owning node.
	pin(const std::string& name_r) : named(name_r) {}

	virtual ~pin() {}

	//!\brief The direction of this pin.
	virtual dir direction() const = 0;

	//!\brief Connect this pin to another pin with a pipe.
	//!
	//! The connection must be between pins of opposing direction.
	//! It is assumed that this call is made through either inpin::connect or outpin::connect, thus that this pin is an inpin and that other is an outpin.
	//! If the output pin is already connected to a pipe, it will be disconnected.
	//! If the input pin is already connected to a pipe, it will be reused.
	//!
	//!\param other The pin to which to connect this pin.
	//!\param max_length The maximum length to give the pipe.
	//!\param max_weight The maximum weight to give the pipe.
	virtual void connect(pin* other, const size_t max_length = 0, const size_t max_weight = 0)
	{
		assert(other);
		assert(direction() == in);
		assert(other->direction() == out);

		// Disconnect the input of the other's pipe.
		if(other->d_pipe_cr_sp)
		{
			auto other_pipe_a = other->d_pipe_cr_sp->access();
			if(other_pipe_a->input()){
				reinterpret_cast<pin*>(other_pipe_a->input())->disconnect();
			}
		}

		if(d_pipe_cr_sp)
		{
			// This inpin already has a pipe, connect the outpin to it.
			auto this_pipe_a = d_pipe_cr_sp->access();
		
			other->d_pipe_cr_sp = d_pipe_cr_sp;
			this_pipe_a->rename(other->name() + "_to_" + name());

			if(max_length) this_pipe_a->cap_length(max_length);
			if(max_weight) this_pipe_a->cap_length(max_weight);
		}
		else
		{
			// This inpin has no pipe, make a new one.
			pipe p(other->name() + "_to_" + name(), reinterpret_cast<outpin*>(other), reinterpret_cast<inpin*>(this), max_length, max_weight);
			other->d_pipe_cr_sp = d_pipe_cr_sp = std::make_shared<lwsync::critical_resource<pipe> >(std::move(p)); 
		}
	}

	//!\brief Disconnects this pin from its pipe.
	virtual void disconnect()
	{
		d_pipe_cr_sp.reset();
	}
};

//!\brief Object that represents a node inlet.
//!
//! Nodes that consume packets, i.e. transformers and consumers, have at least one inpin.
class inpin : public pin
{
	lwsync::monitor<state> &d_state_m_r;

public:
	//!\brief Constructor that takes a name and a reference to the consuming node's state monitor.
	//!
	//! If the node's state is flow::started and a packet arrives, the node's state will be transitioned to flow::incoming.
	//!
	//!\param name_r The name to give this node.
	//!\param state_m_r Reference to the node's state monitor.
	inpin(const std::string& name_r, lwsync::monitor<state>& state_m_r) : pin(name_r), d_state_m_r(state_m_r) {}

	virtual ~inpin() {}

	//!\brief The direction of this pin.
	//!
	//!\return pin::in.
	virtual dir direction() const { return in; }

	//!\brief Connects this pin to an output pin.
	//!
	//!\param outpin_r The output pin this pin will be connected to.
	//!\param max_length The maximum length to give the pipe.
	//!\param max_weight The maximum weight to give the pipe.
	virtual void connect(outpin& outpin_r, const size_t max_length = 0, const size_t max_weight = 0)
	{
		// We must use "dumb" casting here because outpin has not yet been defined as deriving from pin.
		return this->pin::connect(reinterpret_cast<pin*>(&outpin_r), max_length, max_weight);
	}

	//!\brief Check whether a packet is in the pipe.
	//!
	//!\return false if the pipe is empty, true otherwise
	virtual bool peek() const
	{
		if(!d_pipe_cr_sp) return false;

		return d_pipe_cr_sp->const_access()->length() != 0;
	}

	//!\brief Extracts a packet from the pipe.
	//!
	//!\return The next packet to be consumed if the inpin is connected to ap pipe and the pipe is not empty, nullptr otherwise.
	virtual std::unique_ptr<packet> pop()
	{
		if(!d_pipe_cr_sp) return nullptr;

		auto pipe_m_a = d_pipe_cr_sp->access();
		return pipe_m_a->length() ? pipe_m_a->pop() : nullptr;
	}

	//!\brief Notifies this pin that a packet has been queued to the pipe.
	//!
	//! When a producing node has moved a packet to the pipe, that node's outpin will call this function on the connected inpin.
	//! If this inpin's owning node state is flow::::started, it sets the state to flow::incoming to signal the node there is a packet to be consumed.
	virtual void incoming()
	{
		auto state_m_a = d_state_m_r.access();
		if(*state_m_a == started)
		{
			*state_m_a = flow::incoming;
		}
	}
};

//!\brief Object that represents a node outlet.
//!
//! Nodes that produce packets, i.e. producers and transformers, have at least one outpin.
class outpin : public pin
{
public:
	//!\param name_r The name to give this outpin.
	outpin(const std::string& name_r) : pin(name_r) {}

	virtual ~outpin() {}

	//!\brief The direction of this pin.
	//!
	//!\return pin::out.
	virtual dir direction() const { return out; }

	//!\brief Connects this pin to an input pin.
	//!
	//!\param inpin_r The input pin this pin will be connected to.
	//!\param max_length The maximum length to give the pipe.
	//!\param max_weight The maximum weight to give the pipe.
	virtual void connect(inpin& inpin_r, const size_t max_length = 0, const size_t max_weight = 0)
	{
		return inpin_r.connect(*this, max_length, max_weight);
	}

	//!\brief Moves a packet to the pipe.
	//!
	//! Attempts to move the packet on the pipe.
	//! If the pipe has reached capacity, the call to pipe::push will fail and packet_p will remain valid.
	//!
	//!\return true if the packet was successfully moved to the pipe, false otherwise.
	virtual bool push(std::unique_ptr<packet> packet_p)
	{
		if(!d_pipe_cr_sp) return false;

		inpin* inpin_p = nullptr;
		{
			auto pipe_cr_a = d_pipe_cr_sp->access();
			if(pipe_cr_a->push(std::move(packet_p)))
			{
				inpin_p = pipe_cr_a->output();
			}
		}

		if(inpin_p)
		{
			inpin_p->incoming();
		}

		return inpin_p == nullptr;
	}
};

//!\brief Base class common to all nodes.
class node : public named
{
protected:
	lwsync::monitor<state> d_state_m; //!< The state of this node.

public:
	//!\param name_r The name to give this node.
	node(const std::string& name_r) : named(name_r)
	{
		*d_state_m.access() = paused;
	}

	//!\brief Move constructor.
	node(node&& node_rr) : named(node_rr), d_state_m(std::move(node_rr.d_state_m))
	{}

	virtual ~node() {}

	//!\brief Sets this node's state to flow::start_requested.
	//!
	//! The later transition from flow::start_requested to flow::started may not be immediate.
	virtual void start()
	{
		*d_state_m.access() = start_requested;
	}

	//!\brief Sets this node's state to flow::pause_requested.
	//!
	//! The later transition from flow::pause_requested to flow::paused may not be immediate.
	virtual void pause()
	{
		*d_state_m.access() = pause_requested;
	}

	//!\brief Sets this node's state to flow::stop_requested.
	//!
	//! Requests the node to exit from its execution loop.
	//! This may not be immediate.
	virtual void stop()
	{
		*d_state_m.access() = stop_requested;
	}

	//!\brief The node's execution function.
	//!
	//! This is the function that will be called to start execution.
	//! After calling this function, the node's state will be flow::started.
	virtual void operator()() = 0;
};

//!\brief Base class from which concrete pure producers derive.
//!
//! Concrete transformers should derive from flow::transformer.
class producer : public virtual node
{
	std::vector<outpin> d_outputs;

public:
	//!\param name_r The name to give this node.
	//!\param outs Numbers of output pins.
	producer(const std::string& name_r, const size_t outs) : node(name_r)
	{
		for(size_t i = 0; i != outs; ++i)
		{
			d_outputs.push_back(outpin(name_r + "_out" + static_cast<char>('0' + i)));
		}
	}

	virtual ~producer() {}

	//!\brief Returns the number of output pins.
	virtual size_t outs() const { return d_outputs.size(); }

	//!\brief Returns a reference to an outpin pin.
	//!
	//!\param n The index of the output pin.
	virtual outpin& output(const int n) { return d_outputs[n]; }

	//!\brief The node's execution function.
	//!
	//! This is the implementation of node::operator()().
	//! Nodes that are pure producers should use this function as their execution function.
	virtual void operator()()
	{
		state s;
		
		{ s = *d_state_m.access() = started; }

		while(s != stop_requested)
		{
			{
				if(s == paused)
				{
					s = *d_state_m.wait_for([](const state& state_r){ return state_r == start_requested || state_r == stop_requested; });
				}
				else
				{
					s  = *d_state_m.const_access();
				}

				if(s == pause_requested)
				{
					s = *d_state_m.access() = paused;
				}
				else if(s == start_requested)
				{
					s = *d_state_m.access() = started;
				}
			}

			if(s == started)
			{
				produce();
			}
		}
	}

	//!\brief Producing function.
	//!
	//! This function is called from the operator()() execution function.
	//! It is the function that the concrete classes of producing nodes implement.
	//! The body of this function should produce packets.
	virtual void produce() = 0;
};

//!\brief Base class from which concrete pure consumers derive.
//!
//! Concrete transformers should derive from flow::transformer.
class consumer : public virtual node
{
	std::vector<inpin> d_inputs;

public:
	//!\param name_r The name to give this node.
	//!\param ins Numbers of input pins.
	consumer(const std::string& name_r, const size_t ins) : node(name_r)
	{
		for(size_t i = 0; i != ins; ++i)
		{
			d_inputs.push_back(inpin(name_r + "_in" + static_cast<char>('0' + i), d_state_m));
		}
	}

	virtual ~consumer() {}

	//!\brief Returns the number of input pins.
	virtual size_t ins() const { return d_inputs.size(); }

	//!\brief Returns a reference to an input pin.
	//!
	//!\param n The index of the input pin.
	virtual inpin& input(const int n) { return d_inputs[n]; }

	//!\brief The node's execution function.
	//!
	//! This is the implementation of node::operator()().
	//! Nodes that are consumers should use this function as their execution function.
	virtual void operator()()
	{
		state s;
		
		{ s = *d_state_m.access() = started; }

		while(s != stop_requested)
		{
			{
				if(s == paused)
				{
					s = *d_state_m.wait_for([](const state& state_r){ return state_r == start_requested || state_r == stop_requested; });
				}
				else if(s == started)
				{
					s = *d_state_m.wait_for([](const state& state_r){ return state_r != started; });
				}
				else
				{
					s  = *d_state_m.const_access();
				}

				if(s == pause_requested)
				{
					s = *d_state_m.access() = paused;
				}
				else if(s == start_requested)
				{
					s = *d_state_m.access() = started;
				}
				else if(s == incoming)
				{
					*d_state_m.access() = started;
				}
			}

			if(s == incoming)
			{
				for(size_t i = 0; i != ins(); ++i)
				{
					if(input(i).peek())
					{
						ready(i);
					}
				}
			}
		}
	}

	//!\brief Consuming function.
	//!
	//! This function is called from the operator()() execution function.
	//! It is the function that the concrete classes of consuming nodes implement.
	//! This function signals that a packet is ready at an input node.
	//!
	//! param n The index of the input pin at which a packet has arrived.
	virtual void ready(size_t n) = 0;
};

//!\brief Base class from which concrete transformers derive.
class transformer : public producer, public consumer
{
	virtual void produce() {}

public:
	//!\param name_r The name to give this node.
	//!\param ins Numbers of input pins.
	//!\param outs Numbers of output pins.
	transformer(const std::string& name_r, const size_t ins, const size_t outs) : node(name_r), producer(name_r, outs), consumer(name_r, ins)
	{}

	virtual ~transformer() {}

	//!\brief Implmentation of node::operator()().
	virtual void operator()() { consumer::operator()(); }
	
	//!\brief consuming::ready() function to be implemented by concrete class.
	virtual void ready(size_t n) = 0;
};

}

#endif