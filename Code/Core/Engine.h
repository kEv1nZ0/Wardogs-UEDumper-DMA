#pragma once

#include <windows.h>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <Memory/Memory.h>
#include <Core/Offset.h>
#include <Core/GName.h>
#include <Core/Enums.h>

struct TArray {
	UINT64 Data;
	UINT32 Count;
	UINT32 Max;
};

struct FName
{
	UINT32 ComparisonIndex;
	UINT32 DisplayIndex;
	UINT32 Number;
};

class UE_UClass;

class UE_UObject
{
protected:
	UINT64 ObjectAddress;
public:
	UE_UObject(UINT64 ObjectAddress) : ObjectAddress(ObjectAddress) {}
	UE_UObject() : ObjectAddress((UINT64)nullptr) {}
	bool operator==(const UE_UObject& Object) const { return Object.ObjectAddress == ObjectAddress; };
	bool operator!=(const UE_UObject& Object) const { return Object.ObjectAddress != ObjectAddress; };

	UINT32 GetIndex() const;
	UE_UClass GetClass() const;
	UE_UObject GetOuter() const;
	UE_UObject GetPackageObject() const;
	std::string GetName() const;
	std::string GetFullName() const;
	std::string GetCppName() const;
	UINT64 GetAddress() const { return ObjectAddress; }
	operator bool() const { return ObjectAddress != (UINT64)nullptr; }

	template<typename Base>
	Base Cast() const { return Base(ObjectAddress); }

	template<typename T>
	bool IsA() const;

	static UE_UClass StaticClass();
};

class UE_AActor : public UE_UObject
{
public:
	static UE_UClass StaticClass();
};

class UE_UField : public UE_UObject
{
public:
	using UE_UObject::UE_UObject; 
	UE_UField GetNext() const;
	static UE_UClass StaticClass();
};

class UE_FFieldClass : public UE_UObject
{
public:
	using UE_UObject::UE_UObject;
	EClassCastFlags GetCastFlags() const;

	bool IsType(EClassCastFlags Flags) const;
};

class UE_FField : public UE_UObject
{
public:
	using UE_UObject::UE_UObject;
	UE_FField GetNext() const;
	std::string GetName() const;
	bool IsB(EClassCastFlags Flags) const;
	UE_FFieldClass GetClass() const;
};

enum class PropertyType {
	Unknown,
	StructProperty,
	ObjectProperty,
	SoftObjectProperty,
	FloatProperty,
	ByteProperty,
	BoolProperty,
	IntProperty,
	Int8Property,
	Int16Property,
	Int64Property,
	UInt16Property,
	UInt32Property,
	UInt64Property,
	NameProperty,
	DelegateProperty,
	SetProperty,
	ArrayProperty,
	WeakObjectProperty,
	StrProperty,
	TextProperty,
	MulticastSparseDelegateProperty,
	EnumProperty,
	DoubleProperty,
	MulticastDelegateProperty,
	ClassProperty,
	MulticastInlineDelegateProperty,
	MapProperty,
	InterfaceProperty
};

class UE_UFunctionProperty : public UE_UField
{
public:
	using UE_UField::UE_UField;
};

class UE_UProperty : public UE_FField
{
public:
	using UE_FField::UE_FField;

	INT32 GetArrayDim() const;
	INT32 GetSize() const;
	INT32 GetOffset() const;
	UINT64 GetPropertyFlags() const;
	UE_UClass GetMetaClass() const;
	UE_UClass GetPropertyClass() const;
	std::pair<EClassCastFlags, std::string> GetType() const;
};

class UE_UStruct : public UE_UField
{
public:
	using UE_UField::UE_UField;

	UE_UStruct GetSuper() const;
	UE_UField GetChildren() const;
	UE_FField GetChildProperties() const;
	INT32 GetSize() const;
	UE_UProperty FindMember(const std::string& MemberName, EClassCastFlags TypeFlags = EClassCastFlags::None) const;
	static UE_UClass StaticClass();
};

class UE_UClass : public UE_UStruct {
public:
	using UE_UStruct::UE_UStruct;
	static UE_UClass StaticClass();
};

class UE_UEnum : public UE_UField
{
public:
	using UE_UField::UE_UField;
	TArray GetNames() const;
	static UE_UClass StaticClass();
};

template<typename T>
bool UE_UObject::IsA() const
{
	auto cmp = T::StaticClass();
	if (!cmp) { return false; }

	// Super chain step guard: torn reads of a live process can produce garbage
	// class pointers that form cycles and would loop forever
	int ChainGuard = 0;
	for (auto super = GetClass(); super; super = super.GetSuper().Cast<UE_UClass>())
	{
		if (++ChainGuard >= 64) return false;
		if (super.ObjectAddress == cmp.ObjectAddress)
		{
			return true;
		}
	}

	return false;
}

class UE_UFunction : public UE_UStruct
{
public:
	using UE_UStruct::UE_UStruct;
	UINT64 GetFunc() const;
	std::string GetFunctionFlags() const;
	static UE_UClass StaticClass();
};

class UE_UScriptStruct : public UE_UStruct
{
public:
	using UE_UStruct::UE_UStruct;
	static UE_UClass StaticClass();
};

class UE_UByteProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;

	UE_UEnum GetEnum() const;
	std::string GetType() const;

	static UE_UClass StaticClass();
};

class UE_UEnumProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;

	UE_UEnum GetEnum() const;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_UInt16Property : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_UInt32Property : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_UInt64Property : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_Int8Property : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_Int16Property : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_IntProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_Int64Property : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_FloatProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_DoubleProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_BoolProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	UINT8 GetFieldMask() const;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_ObjectPropertyBase : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	UE_UClass GetPropertyClass() const;
	static UE_UClass StaticClass();
};

class UE_ObjectProperty : public UE_ObjectPropertyBase
{
public:
	using UE_ObjectPropertyBase::UE_ObjectPropertyBase;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_ClassProperty : public UE_ObjectProperty
{
public:
	using UE_ObjectProperty::UE_ObjectProperty;
	UE_UClass GetMetaClass() const;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_InterfaceProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	UE_UClass GetInterfaceClass() const;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_WeakObjectProperty : public UE_ObjectPropertyBase
{
public:
	using UE_ObjectPropertyBase::UE_ObjectPropertyBase;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_LazyObjectProperty : public UE_ObjectPropertyBase
{
public:
	using UE_ObjectPropertyBase::UE_ObjectPropertyBase;
	std::string GetType() const;

	static UE_UClass StaticClass();
};

class UE_AssetObjectProperty : public UE_ObjectPropertyBase
{
public:
	using UE_ObjectPropertyBase::UE_ObjectPropertyBase;
	std::string GetType() const;

	static UE_UClass StaticClass();
};

class UE_AssetClassProperty : public UE_AssetObjectProperty
{
public:
	using UE_AssetObjectProperty::UE_AssetObjectProperty;

	UE_UClass GetMetaClass() const;
	std::string GetType() const;

	static UE_UClass StaticClass();
};

class UE_NameProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;

	static UE_UClass StaticClass();
};

class UE_StructProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;

	UE_UStruct GetStruct() const;
	std::string GetType() const;

	static UE_UClass StaticClass();
};

class UE_StrProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;

	static UE_UClass StaticClass();
};

class UE_TextProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;

	static UE_UClass StaticClass();
};

class UE_ArrayProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;

	UE_UProperty GetInner() const;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_MapProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;

	UE_UProperty GetKeyProp() const;
	UE_UProperty GetValueProp() const;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_DelegateProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	UE_UFunction GetSignatureFunction() const;
	std::string GetType() const;
	static UE_UClass StaticClass();
};

class UE_MulticastInlineDelegateProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
};

class UE_MulticastSparseDelegateProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
};

class UE_FieldPathProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
};

class UE_SetProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
};

class UE_EnumProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
};

class UE_SoftClassProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
};

class UE_SoftObjectProperty : public UE_UProperty
{
public:
	using UE_UProperty::UE_UProperty;
	std::string GetType() const;
};

class UE_UPackage
{
private:
	struct Member
	{
		std::string Name;
		INT32 Offset = 0;
		INT32 Size = 0;
	};
	struct Function
	{
		std::string FullName;
		std::string CppName;
		std::string Params;
		std::string Flags;
		UINT64 Func;
	};
	struct Struct
	{
		std::string FullName;
		std::string CppName;
		INT32 Inherited = 0;
		INT32 Size = 0;
		std::vector<Member> Members;
		std::vector<Function> Functions;
	};
	struct Enum
	{
		std::string FullName;
		std::string CppName;
		std::vector<std::string> Members;
	};
private:
	std::pair<UINT64 const, std::vector<UE_UObject>>* Package;
	std::vector<Struct> Classes;
	std::vector<Struct> Structures;
	std::vector<Enum> Enums;
private:
	static void GenerateBitPadding(std::vector<Member>* members, INT32 offset, UINT8 bitOffset, UINT8 size);
	static void GeneratePadding(std::vector<Member>* members, INT32* minOffset, UINT8* bitOffset, INT32 maxOffset);
	static void GenerateStruct(UE_UStruct object, std::vector<Struct>& arr);
	static void GenerateEnum(UE_UEnum object, std::vector<Enum>& arr);
	static void SaveStruct(std::vector<Struct>& arr, File file);
	static void SaveEnum(std::vector<Enum>& arr, File file);
public:
	UE_UPackage(std::pair<UINT64 const, std::vector<UE_UObject>>& package) : Package(&package) {};
	void Process();
	bool Save(const std::filesystem::path& dir);
	UE_UObject GetObject() const;
};