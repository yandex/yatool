#pragma once

#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

#include <type_traits>

/*
    Registers a class to get its singleton later by a name. The registered class must have a default constructor.
    Class constructor is called on demand when an object is requested. Register() doesn't invoke constructors.
    There is independent class collection for the each TBase.
    But any specific class type if it is registered for different TBase's or on different names will share the same object instance (don't forget - it is a singleton factory).
    It may be either useful - to get the same object by different names - or confusing.

    Usage:

    To register the class 'TMyClass' for the base class 'IBase':

        static TClassRegistrar<IBase, TMyClass> uniq_var_name{"TMyClass"};
    or
        static TClassRegistrar<IBase, TMyClass> uniq_var_name{}; // Get registration name from the class name

    To get a class singleton by the name:

        IBase& myObject = TSingletonClassFactory<IBase>::Get()->GetObjectRef("TMyClass"); // throw if TMyClass is not found
    or
        if (IBase* myObjectPtr = TSingletonClassFactory<IBase>::Get()->GetObjectPtr("TMyClass")) {
            myObjectPtr->...
        } else {
            Cerr << "Class TMyClass is not found\n;
        }

    Get all registered names:

        TVector<TString> names = TSingletonClassFactory<IBase>::Get()->GetNames();

    Create and return singletons for all registered classes:

        TVector<IBase*> allObjects = TSingletonClassFactory<IBase>::Get()->GetAllObjects();
*/

namespace NYa {
    template <class TBase>
    class TSingletonClassFactory {
    public:
        using TClassCtor = TBase*(*)();

        static TSingletonClassFactory<TBase>* Get() {
            return Singleton<TSingletonClassFactory<TBase>>();
        }

        template<class TClass, class = std::enable_if_t<std::is_base_of_v<TBase, TClass>>>
        void Register(const TString& name = TypeName(typeid(TClass))) {
            if (ClassCtors_.FindPtr(name)) {
                throw yexception() << "Name '" << name << "' is already registered in the factory for the base class '" << BaseClassName_ << "'";
            }
            ClassCtors_[name] = CreateObject<TClass>;
        }

        TBase& GetObjectRef(const TString& name) const {
            if (TBase* ptr = GetObjectPtr(name)) {
                return *ptr;
            } else {
                throw yexception() << "Name '" << name << "' not found in the factory for the base class '" << BaseClassName_ << "'";
            }
        }

        TBase* GetObjectPtr(const TString& name) const {
            if (const TClassCtor* creatorPtr = ClassCtors_.FindPtr(name)) {
                return (*creatorPtr)();
            } else {
                return nullptr;
            }
        }

        TVector<TString> GetNames() const {
            TVector<TString> names;
            for (const auto& it : ClassCtors_) {
                names.push_back(it.first);
            }
            return names;
        }

        TVector<TBase*> GetAllObjects() const {
            TVector<TBase*> objectPtrs;
            for (const auto& it : ClassCtors_) {
                objectPtrs.push_back(it.second());
            }
            return objectPtrs;
        }

    private:
        inline static const TString BaseClassName_ = TypeName(typeid(TBase));

        template<class TClass>
        static TBase* CreateObject() {
            return Singleton<TClass>();
        }

        THashMap<TString, TClassCtor> ClassCtors_;
    };

    template <class TBase, class TClass>
    struct TClassRegistrar {
        TClassRegistrar(const TString& name = TypeName(typeid(TClass))) {
            TSingletonClassFactory<TBase>::Get()->template Register<TClass>(name);
        }
    };
}
