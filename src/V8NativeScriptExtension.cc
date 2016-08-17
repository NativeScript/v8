#include "V8NativeScriptExtension.h"
#include "api.h"
#include "checks.h"
#include "contexts.h"
#include "globals.h"
#include "handles.h"
#include "assembler.h"
#include "keys.h"
#include <assert.h>


using namespace v8;

template<typename T>
class unsafe_arr
{
public:
	unsafe_arr()
		: m_capacity(16), m_size(0)
	{
		m_data = alloc_data(m_capacity);
	}

	void push_back(const T& e)
	{
		if (m_size == m_capacity)
		{
			resize();
		}
		m_data[m_size++] = e;
	}

	T* data() const
	{
		return m_data;
	}

	size_t size() const
	{
		return m_size;
	}

	static void release_data(T *data)
	{
		free(data);
	}

private:
	T* alloc_data(size_t size)
	{
		T *data = reinterpret_cast<T*>(malloc(size * sizeof(T)));
		return data;
	}

	void resize()
	{
		size_t capacity = 2 * m_capacity;
		T *data = alloc_data(capacity);
		size_t size = m_size * sizeof(T);
		memcpy(data, m_data, size);
		release_data(m_data);
		m_data = data;
		m_capacity = capacity;
	}

	size_t m_capacity;
	size_t m_size;
	T *m_data;
};


NativeScriptExtension::NativeScriptExtension()
{
}


uint8_t* NativeScriptExtension::GetAddress(const Local<Object>& obj)
{
	i::Handle<i::JSReceiver> h = Utils::OpenHandle(*obj);

	return h->address();
}

Local<Value>* NativeScriptExtension::GetClosureObjects(Isolate *isolate, const Local<Function>& func, int *length)
{
	unsafe_arr< Local<Value> > arr;

	i::Handle<i::JSReceiver> receiver = Utils::OpenHandle(*func);

	bool isFunction = receiver->IsJSFunction();

	if (!isFunction) {
		*length = static_cast<int>(arr.size());
		return arr.data();
	}

	i::Handle<i::JSFunction> f = i::Handle<i::JSFunction>::cast(receiver);

	i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(isolate);

	i::Context *cxt = f->context();

	i::ContextLookupFlags cxtFlags = i::FOLLOW_CHAINS;

	while ((cxt != nullptr) && (!cxt->IsNativeContext()))
	{
		i::JSFunction *closure = cxt->closure();

		if (closure != nullptr)
		{
			i::SharedFunctionInfo *sharedFuncInfo = closure->shared();

			if (sharedFuncInfo != nullptr)
			{
				i::ScopeInfo *si = sharedFuncInfo->scope_info();

				if (si != nullptr)
				{
					int len = si->length();

					for (int i = 0; i < len; i++)
					{
						i::Object *cur = si->get(i);

						if ((cur != nullptr) && (cur->IsString()))
						{
							i::String *s = i::String::cast(cur);

							i::Handle<i::String> name = i::Handle<i::String>(s, internal_isolate);

							i::PropertyAttributes attr;
							i::BindingFlags bf;
							int idx;

							i::Handle<i::Object> o = cxt->Lookup(name, cxtFlags, &idx, &attr, &bf);

							if (idx >= 0)
							{
								i::Handle<i::Context> hndCxt = i::Handle<i::Context>::cast(o);
								i::Handle<i::Object> obj = i::Handle<i::Object>(hndCxt->get(idx), internal_isolate);

								if (!obj.is_null() && obj->IsObject())
								{
									Local<Value> local = Utils::ToLocal(obj);

									arr.push_back(local);
								}
							}
						}
					} // for
				} // si != nullptr
			} // sharedFuncInfo != nullptr
		} // closure != nullptr

		cxt = cxt->previous();
	}

	*length = static_cast<int>(arr.size());
	return arr.data();
}


void NativeScriptExtension::ReleaseClosureObjects(Local<Value>* closureObjects)
{
	unsafe_arr< Local<Value> >::release_data(closureObjects);
}


void NativeScriptExtension::GetAssessorPair(Isolate *isolate, const Local<Object>& obj, const Local<String>& propName, Local<Value>& getter, Local<Value>& setter)
{
	i::Handle<i::JSObject> o = i::Handle<i::JSObject>::cast(Utils::OpenHandle(*obj));

	i::Handle<i::String> intname = Utils::OpenHandle(*propName);

	//Isolate* isolate = object->GetIsolate();
	
	internal::LookupIterator it(o, intname, internal::LookupIterator::OWN);
	i::Handle<i::Object> maybe_pair = it.GetAccessors();

	// if (maybe_pair->IsAccessorPair()) {
		i::MaybeHandle<i::Object> g = internal::AccessorPair::GetComponent(i::Handle<internal::AccessorPair>::cast(maybe_pair), i::AccessorComponent::ACCESSOR_GETTER);
		if (!g.is_null())
		{
			getter = Utils::ToLocal(g.ToHandleChecked());
		}

		i::MaybeHandle<i::Object> s = internal::AccessorPair::GetComponent(i::Handle<internal::AccessorPair>::cast(maybe_pair), i::AccessorComponent::ACCESSOR_SETTER);
		if (!s.is_null())
		{
			setter = Utils::ToLocal(s.ToHandleChecked());
		}
	// }
}


Local<Array> NativeScriptExtension::GetPropertyKeys(Isolate *isolate, const Local<Context>& context, const Local<Object>& object, bool& success)
{
	success = true;

	i::Handle<i::JSObject> obj = i::Handle<i::JSObject>::cast(Utils::OpenHandle(*object));
	i::Isolate* internal_isolate = reinterpret_cast<i::Isolate*>(isolate);
	// i::Object* internal_object = reinterpret_cast<i::Object*>(Utils::OpenHandle(*object));

	i::Handle<i::FixedArray> arr = i::KeyAccumulator::GetEnumPropertyKeys(internal_isolate, obj);

	int len = arr->length();

	Local<Array> keys = Array::New(isolate, len);
	for (int i = 0; i < len; i++)
	{
		i::Handle<i::Object> elem = i::Handle<i::Object>(arr->get(i), obj->GetIsolate());
		Local<Value> val = Utils::ToLocal(elem);
		Maybe<bool> res = keys->Set(context, i, val);
		success &= (res.IsJust() && res.FromJust());
	}

	return keys;
}

int NativeScriptExtension::GetInternalFieldCount(const v8::Local<v8::Object>& object)
{
	i::Handle<i::JSObject> obj = i::Handle<i::JSObject>::cast(Utils::OpenHandle(*object));

	int count = obj->GetInternalFieldCount();

	return count;
}

void NativeScriptExtension::CpuFeaturesProbe(bool cross_compile) {
	internal::CpuFeatures::Probe(cross_compile);
}
