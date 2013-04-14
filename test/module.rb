  module Test4RemoveConst
    ExistingConst = 23 
  end
def tgt

  result = Test4RemoveConst.module_eval { remove_const :ExistingConst } 

  name_error = false
  begin
    Test4RemoveConst.module_eval { remove_const :NonExistingConst }
  rescue  NameError
    name_error=true
  end
  p name_error
end
tgt()