#include <Core/Engine.h>
#include <Core/GObject.h>
#include <format>
#include <regex>
#include <string>

inline std::string GetNameByID(const UINT32 ID)
{
	auto Name = GName::ResolveName(ID);
	auto Pos = Name.rfind('/');
	if (Pos != std::string::npos)
	{
		Name = Name.substr(Pos + 1);
	}
	return Name;
}

UINT32 UE_UObject::GetIndex() const
{
	return mem.ReadUseCache<UINT32>(ObjectAddress + Offset.UObject.Index);
};

UE_UClass UE_UObject::GetClass() const
{
	UINT64 classPtr = mem.ReadUseCache<UINT64>(ObjectAddress + Offset.UObject.Class);
	return UE_UClass(classPtr);
};

UE_UObject UE_UObject::GetOuter() const
{
	UINT64 outerPtr = mem.ReadUseCache<UINT64>(ObjectAddress + Offset.UObject.Outer);
	return UE_UObject(outerPtr);
};

std::string UE_UObject::GetName() const
{
	return GetNameByID(mem.ReadUseCache<UINT32>(ObjectAddress + Offset.UObject.Name));
};

std::string UE_UObject::GetFullName() const
{
	std::string TempName;
	// Outer chain step guard: real UE object chains are < 20 deep; torn reads of a
	// live process can produce cyclic/oversized garbage chains where every read
	// still hits a mapped page, so the loop would never terminate (full-dump hang)
	int ChainGuard = 0;
	for (auto Outer = GetOuter(); Outer; Outer = Outer.GetOuter())
	{
		TempName = Outer.GetName() + "." + TempName;
		if (++ChainGuard >= 64) break;
	}

	UE_UClass ObjectClass = GetClass();
	std::string ClassName = ObjectClass.GetName();
	std::string SelfName = GetName();
	return ClassName + " " + TempName + SelfName;
}

std::string UE_UObject::GetCppName() const
{
	std::string name;
	if (IsA<UE_UClass>())
	{
		// Super chain step guard (garbage class pointer cycles)
		int ChainGuard = 0;
		for (auto c = Cast<UE_UStruct>(); c; c = c.GetSuper())
		{
			if (++ChainGuard >= 64) break;
			if (c == UE_AActor::StaticClass())
			{
				name = "A";
				break;
			}
			else if (c == UE_UObject::StaticClass())
			{
				name = "U";
				break;
			}
		}
	}
	else
	{
		name = "F";
	}

	name += GetName();
	return name;
}

UE_UClass UE_UObject::StaticClass()
{
	static auto Object = GObject::FindObject("Class CoreUObject.Object");
	return Object;
};

UE_UClass UE_AActor::StaticClass()
{
	static auto obj = GObject::FindObject("Class Engine.Actor");
	return obj;
}

UE_UClass UE_UField::StaticClass()
{
	static auto Object = GObject::FindObject("Class CoreUObject.Field");
	return Object;
};

UE_UClass UE_UStruct::StaticClass()
{
	static auto Object = GObject::FindObject("Class CoreUObject.Struct");
	return Object;
};

UE_UClass UE_UByteProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.ByteProperty");
	return obj;
};

UE_UClass UE_UClass::StaticClass()
{
	static auto Object = GObject::FindObject("Class CoreUObject.Class");
	return Object;
};

UE_UClass UE_UEnum::StaticClass()
{
	static auto Object = GObject::FindObject("Class CoreUObject.Enum");
	return Object;
}

UE_UClass UE_UFunction::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.Struct");
	return obj;
}

UINT64 UE_UFunction::GetFunc() const
{
	return mem.ReadUseCache<UINT64>(ObjectAddress + Offset.UFunction.Func);
}

std::string UE_UFunction::GetFunctionFlags() const
{
	auto FunctionFlags = mem.ReadUseCache<EFunctionFlags>(ObjectAddress + Offset.UFunction.FunctionFlags);

	std::string StringFlags;
	if ((INT32)FunctionFlags && (INT32)EFunctionFlags::None) { StringFlags = "None"; }
	else {
		if (FunctionFlags & EFunctionFlags::Final) { StringFlags += "Final|"; }
		if (FunctionFlags & EFunctionFlags::RequiredAPI) { StringFlags += "RequiredAPI|"; }
		if (FunctionFlags & EFunctionFlags::BlueprintAuthorityOnly) { StringFlags += "BlueprintAuthorityOnly|"; }
		if (FunctionFlags & EFunctionFlags::BlueprintCosmetic) { StringFlags += "BlueprintCosmetic|"; }
		if (FunctionFlags & EFunctionFlags::Net) { StringFlags += "Net|"; }
		if (FunctionFlags & EFunctionFlags::NetReliable) { StringFlags += "NetReliable"; }
		if (FunctionFlags & EFunctionFlags::NetRequest) { StringFlags += "NetRequest|"; }
		if (FunctionFlags & EFunctionFlags::Exec) { StringFlags += "Exec|"; }
		if (FunctionFlags & EFunctionFlags::Native) { StringFlags += "Native|"; }
		if (FunctionFlags & EFunctionFlags::Event) { StringFlags += "Event|"; }
		if (FunctionFlags & EFunctionFlags::NetResponse) { StringFlags += "NetResponse|"; }
		if (FunctionFlags & EFunctionFlags::Static) { StringFlags += "Static|"; }
		if (FunctionFlags & EFunctionFlags::NetMulticast) { StringFlags += "NetMulticast|"; }
		if (FunctionFlags & EFunctionFlags::UbergraphFunction) { StringFlags += "UbergraphFunction|"; }
		if (FunctionFlags & EFunctionFlags::MulticastDelegate) { StringFlags += "MulticastDelegate|"; }
		if (FunctionFlags & EFunctionFlags::Public) { StringFlags += "Public|"; }
		if (FunctionFlags & EFunctionFlags::Private) { StringFlags += "Private|"; }
		if (FunctionFlags & EFunctionFlags::Protected) { StringFlags += "Protected|"; }
		if (FunctionFlags & EFunctionFlags::Delegate) { StringFlags += "Delegate|"; }
		if (FunctionFlags & EFunctionFlags::NetServer) { StringFlags += "NetServer|"; }
		if (FunctionFlags & EFunctionFlags::HasOutParms) { StringFlags += "HasOutParms|"; }
		if (FunctionFlags & EFunctionFlags::HasDefaults) { StringFlags += "HasDefaults|"; }
		if (FunctionFlags & EFunctionFlags::NetClient) { StringFlags += "NetClient|"; }
		if (FunctionFlags & EFunctionFlags::DLLImport) { StringFlags += "DLLImport|"; }
		if (FunctionFlags & EFunctionFlags::BlueprintCallable) { StringFlags += "BlueprintCallable|"; }
		if (FunctionFlags & EFunctionFlags::BlueprintEvent) { StringFlags += "BlueprintEvent|"; }
		if (FunctionFlags & EFunctionFlags::BlueprintPure) { StringFlags += "BlueprintPure|"; }
		if (FunctionFlags & EFunctionFlags::EditorOnly) { StringFlags += "EditorOnly|"; }
		if (FunctionFlags & EFunctionFlags::Const) { StringFlags += "Const|"; }
		if (FunctionFlags & EFunctionFlags::NetValidate) { StringFlags += "NetValidate|"; }
		if (StringFlags.size()) { StringFlags.erase(StringFlags.size() - 1); };
	}
	return StringFlags;
}

UE_UClass UE_UScriptStruct::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.ScriptStruct");
	return obj;
}

UE_UStruct UE_UStruct::GetSuper() const
{
	return mem.ReadUseCache<UE_UStruct>(ObjectAddress + Offset.UStruct.SuperStruct);
};

UE_UField UE_UStruct::GetChildren() const
{
	return mem.ReadUseCache<UE_UField>(ObjectAddress + Offset.UStruct.Children);
};

UE_FField UE_UStruct::GetChildProperties() const
{
	return mem.ReadUseCache<UE_FField>(ObjectAddress + Offset.UStruct.ChildProperties);
}

INT32 UE_UStruct::GetSize() const
{
	return mem.ReadUseCache<INT32>(ObjectAddress + Offset.UStruct.PropertiesSize);
};

UE_UProperty UE_UStruct::FindMember(const std::string& MemberName, EClassCastFlags TypeFlags) const
{
	if (!ObjectAddress)
		return (UINT64)nullptr;

	// walk guard: corrupted linked lists must not loop forever
	int safety = 0;
	for (UE_FField Field = GetChildProperties(); Field; Field = Field.GetNext())
	{
		if (++safety > 4096) {
			break;
		}
		const auto Name = Field.GetName();
		if (Field.IsB(TypeFlags) && Name == MemberName)
		{
			return Field.Cast<UE_UProperty>();
		}
	}
	return (UINT64)nullptr;
};

UE_UField UE_UField::GetNext() const
{
	// canonicality filter: garbage from freed/torn slots becomes null
	UINT64 next = mem.ReadUseCache<UINT64>(ObjectAddress + Offset.UField.Next);
	if (next < 0x10000ULL || next >= 0x800000000000ULL) return UE_UField((UINT64)nullptr);
	return UE_UField(next);
};

bool UE_FField::IsB(EClassCastFlags Flags) const
{
	return (Flags != EClassCastFlags::None ? GetClass().IsType(Flags) : true);
}

UE_FField UE_FField::GetNext() const
{
	// canonicality filter: garbage from freed/torn slots becomes null
	UINT64 next = mem.ReadUseCache<UINT64>(ObjectAddress + Offset.FField.Next);
	if (next < 0x10000ULL || next >= 0x800000000000ULL) return UE_FField((UINT64)nullptr);
	return UE_FField(next);
};

UE_FFieldClass UE_FField::GetClass() const
{
	return mem.ReadUseCache<UE_FFieldClass>(ObjectAddress + Offset.FField.Class);
};

EClassCastFlags UE_FFieldClass::GetCastFlags() const
{
	return mem.ReadUseCache<EClassCastFlags>(ObjectAddress + Offset.FFieldClass.CastFlags);
}

bool UE_FFieldClass::IsType(EClassCastFlags Flags) const
{
	return (Flags != EClassCastFlags::None ? (GetCastFlags() & Flags) : true);
}

std::string UE_FField::GetName() const
{
	return GetNameByID(mem.ReadUseCache<UINT32>(ObjectAddress + Offset.FField.Name));
};

UE_UObject UE_UObject::GetPackageObject() const
{
	UE_UObject package((UINT64)nullptr);
	// Outer chain step guard (same as GetFullName)
	int ChainGuard = 0;
	for (auto outer = GetOuter(); outer; outer = outer.GetOuter())
	{
		package = outer;
		if (++ChainGuard >= 64) break;
	}
	return package;
}

TArray UE_UEnum::GetNames() const
{
	return mem.ReadUseCache<TArray>(ObjectAddress + Offset.UEnum.Names);
}

void UE_UPackage::GenerateEnum(UE_UEnum object, std::vector<Enum>& arr)
{
	Enum e;
	e.FullName = object.GetFullName();
	e.CppName = "enum class " + object.GetName() + " : UINT8";
	auto names = object.GetNames();

	for (auto i = 0ull; i < names.Count; i++)
	{
		auto NameID = mem.ReadUseCache<UINT32>(names.Data + i * 0x10);
		auto Name = GetNameByID(NameID);
		//std::cout << Name << std::endl;
		e.Members.push_back(Name);
	}

	if (e.Members.size())
	{
		arr.push_back(e);
	}
}

std::pair<EClassCastFlags, std::string> UE_UProperty::GetType() const
{
	EClassCastFlags TypeFlags = GetClass().GetCastFlags();

	//return { TypeFlags, Cast<UE_ClassProperty>().GetType() };

	//return { TypeFlags, std::format("EClassCastFlags::0x{:0X}", (UINT64)TypeFlags) };

	if (TypeFlags & EClassCastFlags::ByteProperty)
	{
		return { TypeFlags, Cast<UE_UByteProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::UInt16Property)
	{
		return { TypeFlags, "UINT16" };
	}
	else if (TypeFlags & EClassCastFlags::UInt32Property)
	{
		return { TypeFlags, "UINT32" };
	}
	else if (TypeFlags & EClassCastFlags::UInt64Property)
	{
		return { TypeFlags, "UINT64" };
	}
	else if (TypeFlags & EClassCastFlags::Int8Property)
	{
		return { TypeFlags, "INT8" };
	}
	else if (TypeFlags & EClassCastFlags::Int16Property)
	{
		return { TypeFlags, "INT16" };
	}
	else if (TypeFlags & EClassCastFlags::IntProperty)
	{
		return { TypeFlags, "INT32" };
	}
	else if (TypeFlags & EClassCastFlags::Int64Property)
	{
		return { TypeFlags, "INT64" };
	}
	else if (TypeFlags & EClassCastFlags::FloatProperty)
	{
		return { TypeFlags, "float" };
	}
	else if (TypeFlags & EClassCastFlags::DoubleProperty)
	{
		return { TypeFlags, "double" };
	}
	else if (TypeFlags & EClassCastFlags::ClassProperty)
	{
		return { TypeFlags, Cast<UE_ClassProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::NameProperty)
	{
		return { TypeFlags, "struct FName" };
	}
	else if (TypeFlags & EClassCastFlags::StrProperty)
	{
		return { TypeFlags, "struct FString" };
	}
	else if (TypeFlags & EClassCastFlags::TextProperty)
	{
		return { TypeFlags, "struct FText" };
	}
	else if (TypeFlags & EClassCastFlags::BoolProperty)
	{
		return { TypeFlags, Cast<UE_BoolProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::StructProperty)
	{
		return { TypeFlags, Cast<UE_StructProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::ArrayProperty)
	{
		return { TypeFlags, Cast<UE_ArrayProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::WeakObjectProperty)
	{
		return { TypeFlags, Cast<UE_WeakObjectProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::LazyObjectProperty)
	{
		return { TypeFlags, Cast<UE_LazyObjectProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::SoftClassProperty)
	{
		return { TypeFlags, Cast<UE_SoftClassProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::SoftObjectProperty)
	{
		return { TypeFlags, Cast<UE_SoftObjectProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::ObjectProperty)
	{
		return { TypeFlags, Cast<UE_ObjectProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::MapProperty)
	{
		return { TypeFlags, Cast<UE_MapProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::SetProperty)
	{
		return { TypeFlags, Cast<UE_SetProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::EnumProperty)
	{
		return { TypeFlags, Cast<UE_EnumProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::InterfaceProperty)
	{
		return { TypeFlags, Cast<UE_InterfaceProperty>().GetType() };
	}
	/*else if (TypeFlags & EClassCastFlags::MulticastDelegateProperty)
	{
		return { TypeFlags,  Cast<UE_MulticastDelegateProperty>().GetType() };
	}*/

	else if (TypeFlags & EClassCastFlags::DelegateProperty)
	{
		return { TypeFlags, Cast<UE_DelegateProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::FieldPathProperty)
	{
		//struct UEnvQueryManager : UAISubsystem
		return { TypeFlags, Cast<UE_FieldPathProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::MulticastInlineDelegateProperty)
	{
		return { TypeFlags, Cast<UE_MulticastInlineDelegateProperty>().GetType() };
	}
	else if (TypeFlags & EClassCastFlags::MulticastSparseDelegateProperty)
	{
		return { TypeFlags, Cast<UE_MulticastSparseDelegateProperty>().GetType() };
	}
	/*else if (TypeFlags & EClassCastFlags::OptionalProperty)
	{
		return Cast<UE_OptionalProperty>().GetType();
	}*/
	else
	{
		return { TypeFlags, std::format("EClassCastFlags_0x{:0X}", (UINT64)TypeFlags)};
	}
}

INT32 UE_UProperty::GetArrayDim() const
{
	return mem.ReadUseCache<INT32>(ObjectAddress + Offset.UProperty.ArrayDim);
}

INT32 UE_UProperty::GetOffset() const
{
	return mem.ReadUseCache<INT32>(ObjectAddress + Offset.UProperty.Offset);
}

UINT64 UE_UProperty::GetPropertyFlags() const
{
	return mem.ReadUseCache<UINT64>(ObjectAddress + Offset.UProperty.PropertyFlags);
}

UE_UClass UE_UProperty::GetMetaClass() const
{
	return mem.ReadUseCache<UE_UClass>(ObjectAddress + Offset.UProperty.PropertyClass + 8);
}

UE_UClass UE_UProperty::GetPropertyClass() const
{
	return mem.ReadUseCache<UE_UClass>(ObjectAddress + Offset.UProperty.PropertyClass);
}

INT32 UE_UProperty::GetSize() const
{
	return mem.ReadUseCache<INT32>(ObjectAddress + Offset.UProperty.ElementSize);
}

UE_UEnum UE_UByteProperty::GetEnum() const
{
	return mem.ReadUseCache<UE_UEnum>(ObjectAddress + Offset.UProperty.PropertyClass);
}

std::string UE_UByteProperty::GetType() const
{
	auto e = GetEnum();
	if (e) { return "enum class " + GetEnum().GetName(); }
	return "bool";
}

UE_UClass UE_DelegateProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.DelegateProperty");
	return obj;
}

std::string UE_MulticastInlineDelegateProperty::GetType() const
{
	return "struct FMulticastInlineDelegate";
}

std::string UE_MulticastSparseDelegateProperty::GetType() const
{
	return "struct FMulticastSparseDelegate";
}

std::string UE_FieldPathProperty::GetType() const
{
	return std::format("TFieldPath<struct {}>", mem.ReadUseCache<UE_FFieldClass>(ObjectAddress + Offset.UProperty.PropertyClass).GetCppName());
}

std::string UE_SetProperty::GetType() const
{
	return std::format("struct TSet<{}>", mem.ReadUseCache<UE_UProperty>(ObjectAddress + Offset.UProperty.PropertyClass - 8).GetType().second);
	//return std::format("TSet<0x{:0X}>", ObjectAddress);
}

std::string UE_DelegateProperty::GetType() const
{
	return std::format("struct TDeleage<GetCppTypeIsNotImplementedForDelegates>");
}

std::string UE_EnumProperty::GetType() const
{
	return std::format("enum class {}", mem.ReadUseCache<UE_UObject>(ObjectAddress + Offset.UProperty.PropertyClass).GetName());
}

UE_UProperty UE_MapProperty::GetKeyProp() const
{
	return mem.ReadUseCache<UE_UProperty>(ObjectAddress + Offset.UProperty.PropertyClass);
}

UE_UProperty UE_MapProperty::GetValueProp() const
{
	return mem.ReadUseCache<UE_UProperty>(ObjectAddress + Offset.UProperty.PropertyClass + 8u);
}

std::string UE_MapProperty::GetType() const
{
	return std::format("struct TMap<{}, {}>", GetKeyProp().GetType().second, GetValueProp().GetType().second);
}

UE_UClass UE_MapProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.MapProperty");
	return obj;
}

UE_UProperty UE_ArrayProperty::GetInner() const
{
	return mem.ReadUseCache<UE_UProperty>(ObjectAddress + Offset.UProperty.PropertyClass);
}

std::string UE_ArrayProperty::GetType() const
{
	return "struct TArray<" + GetInner().GetType().second + ">";
}

UE_UClass UE_ArrayProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.ArrayProperty");
	return obj;
}

std::string UE_StrProperty::GetType() const
{
	return "struct FString";
}

UE_UClass UE_StrProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.StrProperty");
	return obj;
}

std::string UE_TextProperty::GetType() const
{
	return "struct FText";
}

UE_UClass UE_TextProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.TextProperty");
	return obj;
}

std::string UE_NameProperty::GetType() const
{
	return  "struct FName";
}

UE_UClass UE_NameProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.NameProperty");
	return obj;
}

UE_UStruct UE_StructProperty::GetStruct() const
{
	return mem.ReadUseCache<UE_UStruct>(ObjectAddress + Offset.UProperty.PropertyClass);
}

std::string UE_StructProperty::GetType() const
{
	return "struct " + GetStruct().GetCppName();
}

UE_UClass UE_StructProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.StructProperty");
	return obj;
}

std::string UE_AssetObjectProperty::GetType() const
{
	return "struct TAssetPtr<struct " + GetPropertyClass().GetCppName() + ">";;
}

UE_UClass UE_AssetObjectProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.AssetObjectProperty");
	return obj;
}

UE_UClass UE_AssetClassProperty::GetMetaClass() const
{
	return mem.ReadUseCache<UE_UClass>(ObjectAddress + Offset.UProperty.PropertyClass);
}

std::string UE_AssetClassProperty::GetType() const
{
	return "struct " + GetMetaClass().GetCppName() + "*";
}

UE_UClass UE_AssetClassProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.AssetClassProperty");
	return obj;
}

std::string UE_WeakObjectProperty::GetType() const
{
	return "struct TWeakObjectPtr<struct " + GetPropertyClass().GetCppName() + ">";
}

UE_UClass UE_WeakObjectProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.WeakObjectProperty");
	return obj;
}

std::string UE_LazyObjectProperty::GetType() const
{
	return "struct TLazyObjectPtr<struct " + GetPropertyClass().GetCppName() + ">";
}

UE_UClass UE_LazyObjectProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.LazyObjectProperty");
	return obj;
}


std::string UE_UInt16Property::GetType() const
{
	return "uint16";
}

UE_UClass UE_UInt16Property::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.UInt16Property");
	return obj;
}

std::string UE_UInt32Property::GetType() const
{
	return "uint32";
}

UE_UClass UE_UInt32Property::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.UInt32Property");
	return obj;
}

std::string UE_UInt64Property::GetType() const
{
	return "uint64";
}

UE_UClass UE_UInt64Property::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.UInt64Property");
	return obj;
}

std::string UE_Int8Property::GetType() const
{
	return "int8";
}

UE_UClass UE_Int8Property::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.Int8Property");
	return obj;
}

std::string UE_Int16Property::GetType() const
{
	return "int16";
}

UE_UClass UE_Int16Property::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.Int16Property");
	return obj;
}

std::string UE_IntProperty::GetType() const
{
	return "int32";
}

UE_UClass UE_IntProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.IntProperty");
	return obj;
}

std::string UE_Int64Property::GetType() const
{
	return "int64";
}

UE_UClass UE_Int64Property::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.Int64Property");
	return obj;
}

std::string UE_FloatProperty::GetType() const
{
	return "float";
}

UE_UClass UE_FloatProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.FloatProperty");
	return obj;
}

std::string UE_DoubleProperty::GetType() const
{
	return "double";
}

UE_UClass UE_DoubleProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.DoubleProperty");
	return obj;
}

UINT8 UE_BoolProperty::GetFieldMask() const
{
	return mem.ReadUseCache<UINT8>(ObjectAddress + Offset.UProperty.PropertyClass + 3);
}

std::string UE_BoolProperty::GetType() const
{
	if (GetFieldMask() == 0xFF)
	{
		return "bool";
	}
	return "char";
}

UE_UClass UE_BoolProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.BoolProperty");
	return obj;
}

UE_UClass UE_ObjectPropertyBase::GetPropertyClass() const
{
	return mem.ReadUseCache<UE_UClass>(ObjectAddress + Offset.UProperty.PropertyClass);
}

UE_UClass UE_ObjectPropertyBase::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.ObjectPropertyBase");
	return obj;
}

std::string UE_ObjectProperty::GetType() const
{
	return "struct " + GetPropertyClass().GetCppName() + "*";
}

UE_UClass UE_ObjectProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.ObjectPropertyBase");
	return obj;
}

UE_UClass UE_ClassProperty::GetMetaClass() const
{
	return mem.ReadUseCache<UE_UClass>(ObjectAddress + Offset.UProperty.PropertyClass + 8);
}

std::string UE_ClassProperty::GetType() const
{
	return "struct " + GetMetaClass().GetCppName() + "*";
}

UE_UClass UE_ClassProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.ClassProperty");
	return obj;
}

UE_UClass UE_InterfaceProperty::GetInterfaceClass() const
{
	return mem.ReadUseCache<UE_UClass>(ObjectAddress + Offset.UProperty.PropertyClass);
}

std::string UE_InterfaceProperty::GetType() const
{
	return "TScriptInterface<struct " + GetInterfaceClass().GetCppName() + ">";
}

UE_UClass UE_InterfaceProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.InterfaceProperty");
	return obj;
}

UE_UEnum UE_UEnumProperty::GetEnum() const
{
	return mem.ReadUseCache<UE_UEnum>(ObjectAddress + Offset.UProperty.PropertyClass + 8);
}

std::string UE_UEnumProperty::GetType() const
{
	return "enum class " + GetEnum().GetName();
}

UE_UClass UE_UEnumProperty::StaticClass()
{
	static auto obj = GObject::FindObject("Class CoreUObject.EnumProperty");
	return obj;
}

std::string UE_SoftClassProperty::GetType() const
{
	return std::format("struct TSoftClassPtr<struct {}>", GetMetaClass() ? GetMetaClass().GetCppName() : GetPropertyClass().GetCppName());
}

std::string UE_SoftObjectProperty::GetType() const
{
	return std::format("struct TSoftObjectPtr<struct {}>", GetPropertyClass() ? GetPropertyClass().GetCppName() : "UObject");
}

void UE_UPackage::GenerateBitPadding(std::vector<Member>* members, INT32 offset, UINT8 bitOffset, UINT8 size)
{
	Member padding;
	padding.Name = std::format("char pad_{:0X}_{} : {}", offset, bitOffset, size);
	padding.Offset = offset;
	padding.Size = 1;
	members->push_back(padding);
}

void UE_UPackage::GeneratePadding(std::vector<Member>* members, INT32* offset, UINT8* bitOffset, INT32 maxOffset)
{
	if (*bitOffset)
	{
		if (*bitOffset < 8) { UE_UPackage::GenerateBitPadding(members, *offset, *bitOffset, 8 - *bitOffset); }
		*bitOffset = 0;
		*offset += 1;
	}
	if (maxOffset > *offset)
	{
		Member padding;
		auto size = maxOffset - *offset;
		padding.Name = std::format("char pad_{:0X}[{:#0x}]", *offset, size);
		padding.Offset = *offset;
		padding.Size = size;
		members->push_back(padding);
		*offset = maxOffset;
	}
}

void UE_UPackage::GenerateStruct(UE_UStruct object, std::vector<Struct>& arr)
{
	Struct s;
	s.Size = object.GetSize();
	if (s.Size == 0) { return; }
	s.Inherited = 0;
	s.FullName = object.GetFullName();
	s.CppName = "struct " + object.GetCppName();

	auto super = object.GetSuper();
	if (super)
	{
		s.CppName += " : " + super.GetCppName();
		s.Inherited = super.GetSize();
	}

	INT32 offset = s.Inherited;
	UINT8 bitOffset = 0;

	// walk guard: torn reads can produce pointer cycles
	int fnGuard = 0;
	for (auto prop = object.GetChildren().Cast<UE_UFunctionProperty>(); prop; prop = prop.GetNext().Cast<UE_UFunctionProperty>())
	{
		if (++fnGuard > 4096) break;
		auto fn = prop.Cast<UE_UFunction>();
		Function f;
		f.FullName = fn.GetFullName();
		f.Flags = fn.GetFunctionFlags();
		f.Func = fn.GetFunc();

		int paramGuard = 0;
		for (auto param = fn.GetChildProperties().Cast<UE_UProperty>(); param; param = param.GetNext().Cast<UE_UProperty>())
		{
			if (++paramGuard > 1024) break;
			auto flags = param.GetPropertyFlags();

			f.CppName = param.GetType().second + " " + fn.GetName();

			if (param.GetArrayDim() > 1)
			{
				f.Params += std::format("{}* {}, ", param.GetType().second, param.GetName());
			}
			else
			{
				f.Params += std::format("{} {}, ", param.GetType().second, param.GetName());
			}
		}

		if (f.Params.size())
		{
			f.Params.erase(f.Params.size() - 2);
		}

		if (f.CppName.size() == 0)
		{
			f.CppName = "void " + fn.GetName();
		}

		s.Functions.push_back(f);
	}

	// walk guard: torn reads can produce pointer cycles
	int propGuard = 0;
	for (auto prop = object.GetChildProperties().Cast<UE_UProperty>(); prop; prop = prop.GetNext().Cast<UE_UProperty>())
	{
		if (++propGuard > 4096) break;
		auto type = prop.GetType();

		if (prop.IsA<UE_UFunction>())
		{
			//std::cout << prop.GetAddress() << std::endl;
			/*auto fn = prop.Cast<UE_UFunction>();
			Function f;
			f.FullName = fn.GetFullName();
			f.Flags = fn.GetFunctionFlags();
			f.Func = fn.GetFunc();

			for (auto param = fn.GetChildren().Cast<UE_UProperty>(); param; param = param.GetNext().Cast<UE_UProperty>())
			{
				auto flags = param.GetPropertyFlags();

				f.CppName = param.GetType().second + " " + fn.GetName();

				if (param.GetArrayDim() > 1)
				{
					f.Params += std::format("{}* {}, ", param.GetType().second, param.GetName());
				}
				else
				{
					f.Params += std::format("{} {}, ", param.GetType().second, param.GetName());
				}
			}

			if (f.Params.size())
			{
				f.Params.erase(f.Params.size() - 2);
			}

			if (f.CppName.size() == 0)
			{
				f.CppName = "void " + fn.GetName();
			}

			s.Functions.push_back(f);*/
		}
		else
		{
			auto arrDim = prop.GetArrayDim();
			Member m;
			m.Size = prop.GetSize() * arrDim;

			// Skip unsupported/uninitialized zero-size members instead of
			// aborting: the old code did `return` here, which dropped the
			// entire rest of the class (and the class itself) from the SDK.
			if (m.Size == 0) { continue; }

			/*auto type = prop.GetType();
			m.Name = type.second + " " + prop.GetName();*/
			
			m.Name = type.second + " " + prop.GetName();
			m.Offset = prop.GetOffset();

			std::string Name = std::regex_replace(std::regex_replace(s.FullName, std::regex("ScriptStruct "), ""), std::regex("Class "), "");
			GameData.Offset.NameToOffset[std::format("{}.{}", Name, prop.GetName())] = m.Offset;

			if (m.Offset > offset)
			{
				UE_UPackage::GeneratePadding(&s.Members, &offset, &bitOffset, m.Offset);
			}

			if (type.first & EClassCastFlags::BoolProperty && *(UINT32*)type.second.data() != 'loob')
			{
				auto boolProp = prop.Cast<UE_BoolProperty>();
				auto mask = boolProp.GetFieldMask();
				int zeros = 0, ones = 0;
				while (mask & ~1) { mask >>= 1; zeros++; }
				while (mask & 1) { mask >>= 1; ones++; }
				if (zeros > bitOffset)
				{
					UE_UPackage::GenerateBitPadding(&s.Members, offset, bitOffset, zeros - bitOffset);
					bitOffset = zeros;
				}
				m.Name += std::format(" : {}", ones);
				bitOffset += ones;
			}
			else
			{
				if (arrDim > 1)
				{
					m.Name += std::format("[{:#04x}]", arrDim);
				}

				offset += m.Size;
			}

			s.Members.push_back(m);
		}
	}

	if (s.Size > offset)
	{
		UE_UPackage::GeneratePadding(&s.Members, &offset, &bitOffset, s.Size);
	}

	arr.push_back(s);
}

void UE_UPackage::Process()
{
	auto& objects = Package->second;
	for (auto& object : objects)
	{
		if (object.IsA<UE_UClass>())
		{
			GenerateStruct(object.Cast<UE_UStruct>(), Classes);
		}
		else if (object.IsA<UE_UScriptStruct>())
		{
			GenerateStruct(object.Cast<UE_UStruct>(), Structures);
		}
		else if (object.IsA<UE_UEnum>())
		{
			GenerateEnum(object.Cast<UE_UEnum>(), Enums);
		}
	}
}

void UE_UPackage::SaveStruct(std::vector<Struct>& arr, File file)
{
	for (auto& s : arr)
	{
		file.WriteFormat("// {}\n// Size: {:#04x} (Inherited: {:#04x})\n{} {{", s.FullName, s.Size, s.Inherited, s.CppName);
		for (auto& m : s.Members)
		{
			file.WriteFormat("\n\t{}; // {:#04x}({:#04x})", m.Name, m.Offset, m.Size);
		}

		if (s.Functions.size())
		{
			fwrite("\n", 1, 1, file);
			for (auto& f : s.Functions)
			{
				file.WriteFormat("\n\t{}({}); // {} // {} // @ {}+{:#08x}", f.CppName, f.Params, f.FullName, f.Flags, GameData.Name + ".exe", f.Func - GameData.Global.Base);
			}
		}

		file.WriteFormat("\n}};\n\n");

		file.Flush();
	}
}

//void UE_UPackage::SaveEnum(std::vector<Enum>& arr, File file)
//{
//	for (auto& e : arr)
//	{
//		file.WriteFormat("// {}\n{} {{", e.FullName, e.CppName);
//		int Offset = 0;
//		for (auto& m : e.Members)
//		{
//			file.WriteFormat("\n\t{} // {}", m, Offset);
//			Offset++;
//		}
//		file.WriteFormat("\n}};\n\n");
//
//		file.Flush();
//	}
//}

void UE_UPackage::SaveEnum(std::vector<Enum>& arr, File file)
{
	for (auto& e : arr)
	{
		// type declaration and comment
		file.WriteFormat("// {}\n{} {{", e.FullName, e.CppName);

		// longest member name
		size_t maxLength = 0;
		for (const auto& m : e.Members)
		{
			std::string cleanName = m;
			size_t colonPos = cleanName.find("::");
			if (colonPos != std::string::npos)
			{
				cleanName = cleanName.substr(colonPos + 2);
			}

			if (cleanName.length() > maxLength)
			{
				maxLength = cleanName.length();
			}
		}

		// account for the " = 0" suffix of the first member
		const size_t firstMemberExtra = 4; // length of " = 0"

		// write members
		for (size_t i = 0; i < e.Members.size(); ++i)
		{
			std::string cleanName = e.Members[i];
			size_t colonPos = cleanName.find("::");
			if (colonPos != std::string::npos)
			{
				cleanName = cleanName.substr(colonPos + 2);
			}

			// special padding for the first member
			size_t paddingLength;
			if (i == 0)
			{
				paddingLength = maxLength + firstMemberExtra - (cleanName.length() + firstMemberExtra) + 4;
			}
			else
			{
				paddingLength = maxLength + firstMemberExtra - cleanName.length() + 4;
			}
			std::string padding(paddingLength, ' ');

			// first member gets an explicit value of 0
			if (i == 0)
			{
				file.WriteFormat("\n\t{} = 0,{}\t// {}", cleanName, padding, i);
			}
			// last member has no trailing comma
			else if (i == e.Members.size() - 1)
			{
				file.WriteFormat("\n\t{}{}\t// {}", cleanName, padding, i);
			}
			// remaining members print normally with a comma
			else
			{
				file.WriteFormat("\n\t{},{}\t// {}", cleanName, padding, i);
			}
		}

		// closing brace and extra newline
		file.WriteFormat("\n}};\n\n");
		file.Flush();
	}
}

UE_UObject UE_UPackage::GetObject() const
{
	return UE_UObject(Package->first);
}

bool UE_UPackage::Save(const std::filesystem::path& dir)
{
	if (!(Classes.size() || Structures.size() || Enums.size()))
	{
		return false;
	}

	std::string packageName = GetObject().GetName();

	char chars[] = "/\\:*?\"<>|";
	for (auto c : chars)
	{
		auto pos = packageName.find(c);
		if (pos != std::string::npos) { packageName[pos] = '_'; }
	}

	if (Classes.size())
	{
		File file(dir / (packageName + "_classes.h"), "w");
		if (!file) { return false; }
		UE_UPackage::SaveStruct(Classes, file);
	}

	if (Structures.size() || Enums.size())
	{
		if (Enums.size())
		{
			File file(dir / (packageName + "_enum.h"), "w");
			if (!file) { return false; }
			UE_UPackage::SaveEnum(Enums, file);
		}

		if (Structures.size())
		{
			File file(dir / (packageName + "_struct.h"), "w");
			if (!file) { return false; }
			UE_UPackage::SaveStruct(Structures, file);
		}
	}

	return true;
}